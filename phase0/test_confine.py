"""Check what a confined instance may reach in the user's home.

  python3 test_confine.py <dir with lrb_coordinator>

A stand-in instance (this script, started by the coordinator as its browser,
so confined like lrb) reports what it can open, in a made-up home: its own
profile to write, the user's NSS database (client certificates, added
authorities) to read and open with NSS but not write, the phishing and
malware lists' file to read but not write, and nothing else of the home.
No lrb needed.
"""

import ctypes
import json
import os
import subprocess
import sys
import tempfile

from lrb_harness import run as runlib

INSTANCE = r'''#!/usr/bin/python3 -I
import ctypes, json, os, sys
home = os.environ["HOME"]
profile = next(a.split("=", 1)[1] for a in sys.argv if a.startswith("--user-data-dir="))
db = home + "/.pki/nssdb"
safety = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--lrb-safety-file=")), None)
def can(action):
    try:
        action()
        return True
    except OSError:
        return False
nss = ctypes.CDLL("libnss3.so")
result = {
    "write profile": can(lambda: open(profile + "/probe", "w").close()),
    "read nssdb": can(lambda: open(db + "/cert9.db", "rb").read(16)),
    # As Chromium opens it: read-write, which SQLite turns into read-only.
    "open nssdb with NSS": nss.NSS_InitReadWrite(("sql:" + db).encode()) == 0,
    "write nssdb": can(lambda: open(db + "/planted", "w").close()),
    "read documents": can(lambda: os.listdir(home + "/Documents")),
    "given the safety file": bool(safety),
    "read safety file": bool(safety) and can(lambda: open(safety, "rb").read(6)),
    "write safety file": bool(safety) and can(lambda: open(safety, "ab").close()),
}
with open(profile + "/result.json", "w") as f:
    json.dump(result, f)
'''


def main():
    out = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="confine-", dir=runlib.ROOT + "/.profiles")
    home = os.path.join(work, "home")
    db = os.path.join(home, ".pki", "nssdb")
    os.makedirs(db)
    os.makedirs(os.path.join(home, "Documents"))
    os.makedirs(os.path.join(home, ".config", "lrb"))
    with open(os.path.join(home, ".config", "lrb", "settings.json"), "w") as f:
        f.write('{"gpu":false}')  # no first-start question
    # A real NSS database, as Chrome leaves one.
    nss = ctypes.CDLL("libnss3.so")
    created = nss.NSS_InitReadWrite(("sql:" + db).encode()) == 0
    nss.NSS_Shutdown()
    # Fresh lists: the coordinator gives windows the file and fetches none.
    adblock_dir = os.path.join(home, ".local", "share", "lrb", "adblock")
    os.makedirs(adblock_dir)
    with open(os.path.join(adblock_dir, "safety.adb"), "w") as f:
        f.write("engine")
    bin_dir = os.path.join(work, "bin")
    os.makedirs(bin_dir)
    instance = os.path.join(bin_dir, "instance")
    with open(instance, "w") as f:
        f.write(INSTANCE)
    os.chmod(instance, 0o755)
    sock = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                        f"lrb-test-{os.getpid()}.sock")
    env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_")
           or k == "XDG_RUNTIME_DIR"}
    env.update(HOME=home, DBUS_SESSION_BUS_ADDRESS="disabled:")
    profiles = os.path.join(home, ".local", "share", "lrb", "sites")
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    check("a test NSS database was made", created)
    try:
        subprocess.run([f"{out}/lrb_coordinator", f"--browser={instance}",
                        f"--profiles-dir={profiles}", f"--socket={sock}",
                        "--adblock-setting=off", "https://example.com/"],
                       env=env, capture_output=True, timeout=30)
    finally:
        if os.path.exists(sock):
            os.unlink(sock)
    try:
        with open(os.path.join(profiles, ".resolver", "result.json")) as f:
            result = json.load(f)
    except OSError:
        result = {}
    check("the instance ran and wrote to its profile", result.get("write profile") is True,
          result)
    check("it reads the user's NSS database", result.get("read nssdb") is True)
    check("NSS opens it (client certificates, added authorities)",
          result.get("open nssdb with NSS") is True)
    check("it can't write it (no planted authorities)",
          result.get("write nssdb") is False and not os.path.exists(os.path.join(db, "planted")))
    check("nor read the rest of the home", result.get("read documents") is False)
    check("it reads the phishing and malware lists' file",
          result.get("given the safety file") is True and result.get("read safety file") is True,
          result)
    check("...but can't change it", result.get("write safety file") is False)
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
