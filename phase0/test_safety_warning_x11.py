"""Check lrb's phishing and malware warning, on a real display.

  python3 test_safety_warning_x11.py <path to lrb>

The updater compiles a local list (served here, in place of the real ones:
--lrb-list-urls) into an engine file; then a window opens on the X11
display ($DISPLAY; XWayland works) for about half a minute. The listed
hosts are subdomains of lrb.test, mapped to a local server, so every page
stays in the one site's window. Keys go to lrb's window only.

1. The updater writes the file from the list (its bare hostnames
   included).
2. A listed page asks before anything is fetched from it; focus is on
   Back, so Enter stays on the page before.
3. Asked again: Tab, Space (Continue anyway) loads it; visited again, no
   question (remembered for the host).
4. A redirect to a listed host asks too.
5. A listed download asks, and Back fetches nothing.
"""

import http.server
import os
import subprocess
import sys
import tempfile
import threading
import time

from lrb_harness import cdp as cdplib
from lrb_harness import run as runlib
from lrb_harness import x11

XK_Return, XK_Tab, XK_space = 0xff0d, 0xff09, 0x20
LIST = b"""! Title: lrb test list
phish.lrb.test
phish2.lrb.test
||www.lrb.test/download/evil.exe^$all
"""
REQUESTS = []  # (host, path) the server was asked for


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        host = self.headers.get("Host", "").split(":")[0]
        REQUESTS.append((host, self.path))
        if self.path == "/list.txt":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(LIST)
            return
        if self.path == "/go":
            self.send_response(302)
            self.send_header("Location", "http://phish2.lrb.test/")
            self.end_headers()
            return
        if self.path.endswith(".exe"):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.end_headers()
            self.wfile.write(b"MZ")
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(f"<title>{host}</title>{host}".encode())

    def log_message(self, *args):
        pass


def main():
    binary = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="safety-warning-", dir=runlib.ROOT + "/.profiles")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    start_url = "http://www.lrb.test/"
    phish_url = "http://phish.lrb.test/"
    engine = os.path.join(work, "safety.adb")
    env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS="disabled:")
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    updater = subprocess.run(
        [binary, f"--lrb-update-lists={engine}", "--lrb-adblock-setting=safety",
         f"--lrb-list-urls=http://127.0.0.1:{port}/list.txt",
         f"--user-data-dir={work}/updater", "--ozone-platform=x11"],
        env=env, capture_output=True, timeout=60)
    check("the updater compiles the list into the engine file",
          updater.returncode == 0 and os.path.getsize(engine) > 0
          if os.path.exists(engine) else False, updater.returncode)

    log = os.path.join(work, "log")
    proc = subprocess.Popen(
        [binary, f"--user-data-dir={work}/profile", "--lrb-site=lrb.test",
         f"--lrb-safety-file={engine}", f"--host-resolver-rules=MAP *.lrb.test:80 127.0.0.1:{port}",
         "--ozone-platform=x11", "--remote-debugging-port=0", "--single-process",
         "--no-zygote", "--no-sandbox", start_url],
        stdout=open(log, "wb"), stderr=subprocess.STDOUT, env=env,
        start_new_session=True)

    def keys(*keysyms):
        window = x11.windows()[0][0]
        for keysym in keysyms:
            x11.key(window, keysym, 0)
            time.sleep(0.3)

    def title(client, session):
        time.sleep(2)
        try:
            return client.evaluate(session, "document.title")
        except cdplib.CDPError:
            return None

    def window_title():
        """The shown page's title while a question holds a navigation (and
        DevTools with it)."""
        time.sleep(2)
        return x11.windows()[0][1]

    def asked_for(host, path="/"):
        return (host, path) in REQUESTS

    try:
        client = cdplib.CDP(runlib.wait_for_devtools(proc, log))
        session = client.attach_first_page()
        runlib.wait_ready(client, session, start_url, 20)
        client.call("Emulation.setFocusEmulationEnabled", {"enabled": True}, session)
        time.sleep(1)

        client.send("Page.navigate", {"url": phish_url}, session)
        shown = window_title()
        check("a listed page doesn't load before an answer", shown == "www.lrb.test", shown)
        check("...and nothing is fetched from it", not asked_for("phish.lrb.test"))
        keys(XK_Return)
        shown = title(client, session)
        check("Enter (focus on Back) stays on the page before",
              shown == "www.lrb.test" and not asked_for("phish.lrb.test"), shown)

        client.send("Page.navigate", {"url": phish_url}, session)
        time.sleep(2)
        keys(XK_Tab, XK_space)
        shown = title(client, session)
        check("Tab, Space (Continue anyway) loads it", shown == "phish.lrb.test", shown)

        client.call("Page.navigate", {"url": start_url}, session)
        runlib.wait_ready(client, session, start_url, 10)
        client.send("Page.navigate", {"url": phish_url}, session)
        shown = title(client, session)
        check("visited again, it loads without a question", shown == "phish.lrb.test", shown)

        client.call("Page.navigate", {"url": start_url}, session)
        runlib.wait_ready(client, session, start_url, 10)
        client.send("Page.navigate", {"url": start_url + "go"}, session)
        shown = window_title()
        check("a redirect to a listed host asks first",
              shown == "www.lrb.test" and not asked_for("phish2.lrb.test"), shown)
        keys(XK_Return)

        client.send("Page.navigate", {"url": start_url + "download/evil.exe"}, session)
        time.sleep(2)
        keys(XK_Return)
        time.sleep(1)
        check("a listed download asks, and Back fetches nothing",
              not asked_for("www.lrb.test", "/download/evil.exe"),
              [r for r in REQUESTS if r[1].endswith(".exe")])
    finally:
        runlib.stop(proc, None)
        server.shutdown()
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
