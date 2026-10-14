"""Check what lrb_coordinator hands to other apps (open-app: mailto:, tel:).

  python3 test_open_app.py <dir with lrb_coordinator>

An instance asks the user, then sends the link; the coordinator can't
trust that it asked (a compromised instance wouldn't), so it checks what
it can: only links for apps, never http(s) or files, at most one every
2 s. A stand-in instance (this script, started by the coordinator as its
browser) sends links; a stand-in xdg-open records what reaches it. No lrb
needed. The question in the window: by hand (NEXT.md).
"""

import os
import subprocess
import sys
import tempfile

from lrb_harness import run as runlib

INSTANCE = r'''#!/usr/bin/env python3
import socket, sys, time
path = next(a.split("=", 1)[1] for a in sys.argv if a.startswith("--lrb-coordinator="))
s = socket.socket(socket.AF_UNIX)
s.connect(path)
def send(line):
    s.sendall(line.encode() + b"\n")
send("site example.com")
send("open-app mailto%3Asomeone%40example.com%3Fsubject%3DHola")
send("open-app mailto%3Asecond%40example.com")
time.sleep(2.5)
send("open-app https%3A%2F%2Fexample.org%2F")
send("open-app file%3A%2F%2F%2Fetc%2Fpasswd")
send("open-app -x%3Ay")
send("open-app tel%3A%2B56912345678")
time.sleep(1.5)
'''


def main():
    out = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="open-app-", dir=runlib.ROOT + "/.profiles")
    os.makedirs(os.path.join(work, "bin"))
    os.makedirs(os.path.join(work, "config", "lrb"))
    with open(os.path.join(work, "config", "lrb", "settings.json"), "w") as f:
        f.write('{"gpu":false}')  # no first-start question
    opened = os.path.join(work, "opened")
    with open(os.path.join(work, "bin", "xdg-open"), "w") as f:
        f.write(f"#!/bin/sh\nprintf '%s\\n' \"$*\" >> {opened}\n")
    instance = os.path.join(work, "instance.py")
    with open(instance, "w") as f:
        f.write(INSTANCE)
    for path in (instance, os.path.join(work, "bin", "xdg-open")):
        os.chmod(path, 0o755)
    # Short: a Unix socket's path has a length limit.
    sock = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                        f"lrb-test-{os.getpid()}.sock")
    env = dict(os.environ, PATH=os.path.join(work, "bin") + ":" + os.environ["PATH"],
               XDG_CONFIG_HOME=os.path.join(work, "config"),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    try:
        log = subprocess.run(
            [f"{out}/lrb_coordinator", "--verbose", "--no-confine", f"--browser={instance}",
             f"--profiles-dir={work}/profiles", f"--socket={sock}",
             "--adblock-setting=off", "https://example.com/"],
            env=env, capture_output=True, text=True, timeout=30).stderr
    finally:
        if os.path.exists(sock):
            os.unlink(sock)
    try:
        with open(opened) as f:
            links = f.read().splitlines()
    except OSError:
        links = []
    check("a mailto: link reaches the desktop, decoded",
          "mailto:someone@example.com?subject=Hola" in links, links)
    check("a second link within 2 s doesn't", "mailto:second@example.com" not in links)
    check("http(s) and file: links never reach it",
          not any(l.startswith(("https:", "file:")) for l in links), links)
    check("nothing that xdg-open would take as an option",
          not any(l.startswith("-") for l in links), links)
    check("a tel: link 2 s later does", "tel:+56912345678" in links, links)
    check("refusals are logged", log.count("refused open-app") == 4,
          f"{log.count('refused open-app')} refusals")
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
