"""Check what lrb_coordinator lets instances do with the keyring.

  python3 test_keyring_broker.py <dir with lrb_coordinator>

Saved passwords and autofill details go through the coordinator to the
desktop's keyring (keyring_broker.h). A stand-in instance (this script,
started by the coordinator as its browser) asks; a stand-in lrb_keyring
records what reaches it and answers. A site's instance only reaches its own
site's passwords, for origins on that site; the resolver (no site) none.
No lrb and no keyring needed. LRB_TEST_KEYRING=<lrb_keyring> uses the real
helper instead (run it in a throwaway session: dbus-run-session, a fresh
HOME and `gnome-keyring-daemon --unlock`), skipping the checks of what the
stand-in received.
"""

import json
import os
import subprocess
import sys
import tempfile

from lrb_harness import run as runlib

INSTANCE = r'''#!/usr/bin/env python3
import json, os, socket, sys
args = sys.argv[1:]
path = next(a.split("=", 1)[1] for a in args if a.startswith("--lrb-coordinator="))
profile = next(a.split("=", 1)[1] for a in args if a.startswith("--user-data-dir="))
site = next((a.split("=", 1)[1] for a in args if a.startswith("--lrb-site=")), "")
s = socket.socket(socket.AF_UNIX)
s.connect(path)
f = s.makefile("rw")
def ask(line):
    f.write(line + "\n"); f.flush()
    return f.readline().strip()
answers = {}
if not site:
    # The resolver: no site, so no keyring; then the page's site opens.
    answers["resolver pw-list"] = ask("pw-list 9 https%3A%2F%2Fwww.example.com")
    f.write("open example.com https://www.example.com/\n"); f.flush()
else:
    f.write("site example.com\n"); f.flush()
    answers["save own"] = ask("pw-save 1 https%3A%2F%2Fwww.example.com alice hunter2%20%C3%A9")
    answers["save other site"] = ask("pw-save 2 https%3A%2F%2Fevil.org bob x")
    answers["list"] = ask("pw-list 3 https%3A%2F%2Fwww.example.com")
    answers["get"] = ask("pw-get 4 https%3A%2F%2Fwww.example.com alice")
    answers["get with a path"] = ask("pw-get 5 https%3A%2F%2Fwww.example.com%2Flogin alice")
    answers["autofill save"] = ask("af-save 6 name%3DCarlos%26email%3Dc%2540x.cl")
    answers["autofill get"] = ask("af-get 7")
with open(os.path.join(profile, "answers.json"), "w") as out:
    json.dump(answers, out)
'''

KEYRING = r'''#!/usr/bin/env python3
import json, sys
log = sys.argv[0] + ".log"
stdin = sys.stdin.read()
with open(log, "a") as f:
    f.write(json.dumps({"args": sys.argv[1:], "stdin": stdin}) + "\n")
command, schema = sys.argv[1], sys.argv[2]
if command == "get" and schema == "password":
    print("hunter2 é", end="")
elif command == "get" and schema == "autofill":
    print("name=Carlos&email=c%40x.cl", end="")
elif command == "list":
    print("alice")
'''


def main():
    out = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="keyring-", dir=runlib.ROOT + "/.profiles")
    os.makedirs(os.path.join(work, "config", "lrb"))
    with open(os.path.join(work, "config", "lrb", "settings.json"), "w") as f:
        f.write('{"gpu":false}')  # no first-start question
    bin_dir = os.path.join(work, "bin")
    os.makedirs(bin_dir)
    instance, keyring = os.path.join(bin_dir, "instance"), os.path.join(bin_dir, "keyring")
    for path, text in ((instance, INSTANCE), (keyring, KEYRING)):
        with open(path, "w") as f:
            f.write(text)
        os.chmod(path, 0o755)
    real = os.environ.get("LRB_TEST_KEYRING")
    if real:
        keyring = os.path.abspath(real)
    sock = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                        f"lrb-test-{os.getpid()}.sock")
    profiles = os.path.join(work, "profiles")
    env = dict(os.environ, XDG_CONFIG_HOME=os.path.join(work, "config"))
    if not real:
        env["DBUS_SESSION_BUS_ADDRESS"] = "disabled:"  # the real one needs it
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    try:
        subprocess.run([f"{out}/lrb_coordinator", "--no-confine", f"--browser={instance}",
                        f"--keyring={keyring}", f"--profiles-dir={profiles}",
                        f"--socket={sock}", "--adblock-setting=off",
                        "https://www.example.com/"],
                       env=env, capture_output=True, timeout=30)
    finally:
        if os.path.exists(sock):
            os.unlink(sock)

    def answers(name):
        try:
            with open(os.path.join(profiles, name, "answers.json")) as f:
                return json.load(f)
        except OSError:
            return {}

    resolver, site = answers(".resolver"), answers("example.com")
    try:
        with open(keyring + ".log") as f:
            calls = [json.loads(line) for line in f]
    except OSError:
        calls = []
    check("the resolver (no site) gets no keyring",
          resolver.get("resolver pw-list") == "pw-users 9 unavailable", resolver)
    check("a site saves a password for its own origin",
          site.get("save own") == "pw-saved 1 1", site.get("save own"))
    check("...to the keyring with its site from its profile, the password on stdin",
          real or {"args": ["store", "password", "site=example.com",
                    "origin=https://www.example.com", "username=alice"],
           "stdin": "hunter2 é"} in calls, calls[:1])
    check("an origin of another site is refused",
          site.get("save other site") == "pw-saved 2 0")
    check("...and never reaches the keyring",
          not any("evil.org" in " ".join(c["args"]) for c in calls))
    check("it lists its own users", site.get("list") == "pw-users 3 ok alice",
          site.get("list"))
    check("it gets a password, encoded",
          site.get("get") == "pw 4 ok hunter2%20%C3%A9", site.get("get"))
    check("an origin with a path is refused",
          site.get("get with a path") == "pw 5 none", site.get("get with a path"))
    check("autofill details are saved, shared (no site attribute)",
          site.get("autofill save") == "af-saved 6 1" and
          (real or {"args": ["store", "autofill", "profile=default"],
                    "stdin": "name=Carlos&email=c%40x.cl"} in calls))
    check("...and read back", site.get("autofill get") ==
          "af 7 ok name%3DCarlos%26email%3Dc%2540x.cl", site.get("autofill get"))
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
