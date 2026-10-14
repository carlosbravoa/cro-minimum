"""Check when lrb_coordinator fetches the phishing and malware lists.

  python3 test_safety_lists.py <dir with lrb_coordinator>

The coordinator refreshes the lists' engine file (safety.adb) when it
starts, if the file is missing or over an hour old, and never while it
runs; windows get --lrb-safety-file once the file exists. A stand-in (this
script, started by the coordinator as its browser) records how it was
started; as the updater it writes the file after a moment, as a window it
registers and exits. No lrb and no network needed.

1. First start: the updater runs for the safety lists; the first window
   doesn't wait for it.
2. Started again at once: no download (fresh), and windows get the file.
3. With the file two hours old: downloaded again.
4. With content blocking on and no filter lists yet: the filter lists
   first, then the safety lists, one updater at a time.
5. --safety-lists=off: neither the download nor the file.
"""

import json
import os
import subprocess
import sys
import tempfile
import time

from lrb_harness import run as runlib

STAND_IN = r'''#!/usr/bin/env python3
import json, os, socket, sys, time
args = sys.argv[1:]
log = os.environ["LRB_TEST_LOG"]
def record(event):
    with open(log, "a") as f:
        f.write(json.dumps(dict(event, t=time.time())) + "\n")
update = next((a.split("=", 1)[1] for a in args if a.startswith("--lrb-update-lists=")), None)
if update:
    record({"updater": update, "args": args, "start": True})
    time.sleep(1.5)
    with open(update, "w") as f:
        f.write("engine")
    record({"updater": update, "end": True})
    sys.exit(0)
record({"instance": True, "args": args})
path = next(a.split("=", 1)[1] for a in args if a.startswith("--lrb-coordinator="))
site = next((a.split("=", 1)[1] for a in args if a.startswith("--lrb-site=")), "")
s = socket.socket(socket.AF_UNIX)
s.connect(path)
f = s.makefile("rw")
if not site:
    f.write("open example.com https://www.example.com/\n")
else:
    f.write("site example.com\n")
f.flush()
time.sleep(0.5)
'''


def main():
    out = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="safety-", dir=runlib.ROOT + "/.profiles")
    os.makedirs(os.path.join(work, "config", "lrb"))
    with open(os.path.join(work, "config", "lrb", "settings.json"), "w") as f:
        f.write('{"gpu":false}')  # no first-start question
    stand_in = os.path.join(work, "browser")
    with open(stand_in, "w") as f:
        f.write(STAND_IN)
    os.chmod(stand_in, 0o755)
    data = os.path.join(work, "data")
    safety = os.path.join(data, "lrb", "adblock", "safety.adb")
    sock = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                        f"lrb-test-{os.getpid()}.sock")
    log = os.path.join(work, "log")
    env = dict(os.environ, XDG_CONFIG_HOME=os.path.join(work, "config"),
               XDG_DATA_HOME=data, LRB_TEST_LOG=log,
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    def start(*extra, adblock="off"):
        """Runs the coordinator until its last window exits; what was started."""
        if os.path.exists(log):
            os.remove(log)
        try:
            subprocess.run([f"{out}/lrb_coordinator", "--no-confine",
                            f"--browser={stand_in}", f"--profiles-dir={work}/profiles",
                            f"--socket={sock}", f"--adblock-setting={adblock}", *extra,
                            "https://www.example.com/"],
                           env=env, capture_output=True, timeout=60)
            time.sleep(2)  # an updater still running when the last window left
        finally:
            if os.path.exists(sock):
                os.unlink(sock)
        try:
            with open(log) as f:
                return [json.loads(line) for line in f]
        except OSError:
            return []

    def updaters(events, file=safety):
        return [e for e in events if e.get("updater") == file and e.get("start")]

    def windows_with_file(events):
        windows = [e for e in events if e.get("instance")]
        return windows, [w for w in windows if f"--lrb-safety-file={safety}" in w["args"]]

    events = start()
    runs = updaters(events)
    check("first start: the safety lists are fetched", len(runs) == 1, runs)
    check("...by the updater with the safety setting",
          bool(runs) and "--lrb-adblock-setting=safety" in runs[0]["args"])
    windows, with_file = windows_with_file(events)
    check("...and the first window doesn't wait for them",
          bool(windows) and not with_file and
          bool(runs) and windows[0]["t"] < runs[0]["t"] + 1.5)

    events = start()
    windows, with_file = windows_with_file(events)
    check("started again at once: not fetched again", not updaters(events), updaters(events))
    check("...and windows get the file", bool(windows) and with_file == windows,
          [w["args"] for w in windows])

    old = time.time() - 2 * 3600
    os.utime(safety, (old, old))
    events = start()
    check("with the file two hours old: fetched again", len(updaters(events)) == 1)

    os.utime(safety, (old, old))
    lean = os.path.join(data, "lrb", "adblock", "lean.adb")
    events = start(adblock="lean")
    filter_runs, safety_runs = updaters(events, lean), updaters(events, safety)
    filter_end = [e for e in events if e.get("updater") == lean and e.get("end")]
    check("content blocking on: the filter lists, then the safety lists",
          len(filter_runs) == 1 and len(safety_runs) == 1 and bool(filter_end) and
          safety_runs[0]["t"] >= filter_end[0]["t"],
          [(e.get("updater"), "start" if e.get("start") else "end")
           for e in events if "updater" in e])

    os.utime(safety, (old, old))
    events = start("--safety-lists=off")
    windows, with_file = windows_with_file(events)
    check("--safety-lists=off: not fetched", not updaters(events))
    check("...and windows don't get the file", bool(windows) and not with_file)
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
