#!/usr/bin/env python3
"""lrb against Chromium as installed: README's "Against Chromium" table.

Both browsers from their own launchers, with a window on the desktop, one
page each, a fresh profile per run, no memory limit. Chromium: the snap's
`chromium` (--no-first-run, so no welcome tab). lrb: the installed
`low-ram-browser` (coordinator + confined site process) with the user's own
settings. Memory: every process each browser runs (smaps_rollup), sampled
20, 40 and 60 s after launch; the report uses 60 s, medians, and leaves out
runs that ended on Google's "unusual traffic" page.

  tools/compare_chromium.py OUT_DIR [REPEATS]   # about 35 min for 3
  tools/compare_chromium.py --report OUT_DIR

Needs the desktop (run it from a terminal there); don't use the machine
meanwhile.
"""

import json
import os
import shutil
import signal
import subprocess
import sys
import time

HOME = os.environ["HOME"]
PAGES = [
    ("wikipedia", "https://en.wikipedia.org/wiki/Chromium_(web_browser)"),
    ("github", "https://github.com/chromium/chromium"),
    ("bbc", "https://www.bbc.com/news"),
    ("reddit", "https://www.reddit.com/r/linux/"),
    ("youtube", "https://www.youtube.com/watch?v=aqz-KE-bpKQ"),
]
SAMPLES = (20, 40, 60)
FIELDS = ("Pss", "Pss_Anon", "Pss_Shmem", "Pss_File", "Rss", "SwapPss")


def all_procs():
    procs = {}
    for e in os.listdir("/proc"):
        if not e.isdigit():
            continue
        try:
            with open(f"/proc/{e}/stat") as f:
                ppid = int(f.read().rsplit(")", 1)[1].split()[1])
            with open(f"/proc/{e}/cmdline", "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode(errors="replace")
        except OSError:
            continue
        procs[int(e)] = (ppid, cmd)
    return procs


def browser_pids(root, marker):
    """root's descendants, plus anything naming the profile (crashpad
    reparents itself away from the browser)."""
    procs = all_procs()
    children = {}
    for pid, (ppid, _) in procs.items():
        children.setdefault(ppid, []).append(pid)
    out, stack = set(), [root]
    while stack:
        p = stack.pop()
        if p in procs:
            out.add(p)
        stack.extend(children.get(p, []))
    out |= {p for p, (_, cmd) in procs.items() if marker in cmd}
    return out, procs


def kind(cmd):
    for part in cmd.split():
        if part.startswith("--type="):
            return part[7:]
    base = os.path.basename(cmd.split()[0]) if cmd else "?"
    return base


def sample(root, marker):
    pids, procs = browser_pids(root, marker)
    totals = {k: 0 for k in FIELDS}
    by_kind = {}
    for pid in pids:
        try:
            with open(f"/proc/{pid}/smaps_rollup") as f:
                vals = {}
                for line in f:
                    parts = line.split()
                    k = parts[0].rstrip(":")
                    if k in FIELDS:
                        vals[k] = int(parts[1])
        except OSError:
            continue
        for k, v in vals.items():
            totals[k] += v
        kd = kind(procs[pid][1])
        by_kind[kd] = by_kind.get(kd, 0) + vals.get("Pss", 0)
    return {"processes": len(pids), "totals": totals, "pss_by_kind": by_kind}


def wait_gone(pids, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not any(os.path.exists(f"/proc/{p}") for p in pids):
            return True
        time.sleep(0.3)
    for p in pids:
        try:
            os.kill(p, signal.SIGKILL)
        except OSError:
            pass
    return False


def got_sorry(profile):
    r = subprocess.run(["grep", "-rlas", "google.com/sorry", profile],
                       capture_output=True, text=True)
    return bool(r.stdout.strip())


def run_chromium(url, profile):
    argv = ["/snap/bin/chromium", f"--user-data-dir={profile}",
            "--no-first-run", "--no-default-browser-check", url]
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    # The snap launcher execs into chrome; find the browser process by its
    # profile flag (no --type=).
    root = None
    for _ in range(100):
        for pid, (_, cmd) in all_procs().items():
            if profile in cmd and "--type=" not in cmd and "chrome" in cmd.split()[0]:
                root = pid
                break
        if root:
            break
        time.sleep(0.2)
    return proc, root, profile


def run_lrb(url, profiles):
    argv = [f"{HOME}/.local/bin/low-ram-browser", f"--profiles-dir={profiles}", url]
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    return proc, proc.pid, profiles


def one(browser, page, url, rep, base):
    profile = os.path.join(base, f"{browser}-{page}-{rep}")
    shutil.rmtree(profile, ignore_errors=True)
    os.makedirs(profile)
    t0 = time.monotonic()
    if browser == "chromium":
        proc, root, marker = run_chromium(url, profile)
    else:
        proc, root, marker = run_lrb(url, profile)
    rec = {"browser": browser, "page": page, "repeat": rep, "samples": {}}
    if not root:
        rec["error"] = "browser process not found"
    else:
        for s in SAMPLES:
            time.sleep(max(0, t0 + s - time.monotonic()))
            rec["samples"][str(s)] = sample(root, marker)
    pids, _ = browser_pids(root, marker) if root else (set(), None)
    for p in pids:
        try:
            os.kill(p, signal.SIGTERM)
        except OSError:
            pass
    try:
        os.kill(proc.pid, signal.SIGTERM)
    except OSError:
        pass
    try:
        proc.wait(timeout=15)  # reap the launcher, or its zombie looks alive
    except subprocess.TimeoutExpired:
        pass
    rec["clean_exit"] = wait_gone(pids - {proc.pid})
    rec["sorry_page"] = got_sorry(profile)
    shutil.rmtree(profile, ignore_errors=True)
    return rec


def report(out):
    import statistics
    runs = {}
    with open(os.path.join(out, "runs.jsonl")) as f:
        for line in f:
            r = json.loads(line)
            if r.get("error") or r.get("sorry_page"):
                continue
            t = r["samples"]["60"]["totals"]
            runs.setdefault(r["page"], {}).setdefault(r["browser"], []).append(
                (t["Pss"] / 1024, (t["Pss_Anon"] + t["Pss_Shmem"]) / 1024))
    lines = ["| page | Chromium | lrb | saved | unreclaimable: Chromium | lrb |",
             "|---|---:|---:|---:|---:|---:|"]
    for page, _ in PAGES:
        b = runs.get(page, {})
        if "chromium" not in b or "lrb" not in b:
            lines.append(f"| {page} | - | - | - | - | - |")
            continue
        c, l = ([statistics.median(x[i] for x in b[k]) for i in (0, 1)]
                for k in ("chromium", "lrb"))
        lines.append(f"| {page} | {c[0]:.0f} | {l[0]:.0f} | {100 * (1 - l[0] / c[0]):.0f}% "
                     f"| {c[1]:.0f} | {l[1]:.0f} |")
    return "\n".join(lines)


def main():
    if sys.argv[1:2] == ["--report"]:
        print(report(sys.argv[2]))
        return
    OUT = sys.argv[1]
    REPEATS = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    os.makedirs(OUT, exist_ok=True)
    # Under the snap's home: the confined Chromium can write there.
    base = os.path.join(HOME, "snap/chromium/common/lrb-bench")
    os.makedirs(base, exist_ok=True)
    total = REPEATS * len(PAGES) * 2
    n = 0
    with open(os.path.join(OUT, "runs.jsonl"), "a") as f:
        for rep in range(REPEATS):
            for page, url in PAGES:
                for browser in ("chromium", "lrb"):
                    n += 1
                    rec = one(browser, page, url, rep, base)
                    f.write(json.dumps(rec) + "\n")
                    f.flush()
                    last = rec["samples"].get("60", {}).get("totals", {})
                    print(f"[{n}/{total}] {browser:8} {page:10} "
                          f"{(last.get('Pss', 0)) / 1024:.0f} MB PSS"
                          f"{' (sorry page)' if rec.get('sorry_page') else ''}"
                          f"{' ERROR ' + rec['error'] if rec.get('error') else ''}",
                          flush=True)
                    time.sleep(3)
    shutil.rmtree(base, ignore_errors=True)
    print(report(OUT))


main()
