"""Check that a site's cookies outlive its process: signed in stays signed in.

  python3 test_cookies.py <path to lrb>

content_shell kept cookies in memory only, so every start signed every site
out. A local page sets a persistent cookie and a session cookie; lrb closes
cleanly and starts again on the same profile. The persistent one must be
back, the session one gone (as in Chrome without session restore).
"""

import http.server
import os
import sys
import tempfile
import threading
import time

from lrb_harness import cdp as cdplib
from lrb_harness import run as runlib


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        if self.path == "/set":
            self.send_header("Set-Cookie", "kept=1; Max-Age=3600; Path=/")
            self.send_header("Set-Cookie", "session=1; Path=/")
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(b"<title>cookies</title>cookies")

    def log_message(self, *args):
        pass


def main():
    binary = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="cookies-", dir=runlib.ROOT + "/.profiles")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    def visit(url, n):
        """Starts lrb on the profile, opens `url`, returns document.cookie,
        then closes lrb as the user would (it must flush the cookies)."""
        log = os.path.join(work, f"log{n}")
        proc = runlib.launch([binary, f"--user-data-dir={work}/p", "--ozone-platform=headless",
                              "--remote-debugging-port=0", url], log, None, False)
        client = None
        try:
            client = cdplib.CDP(runlib.wait_for_devtools(proc, log))
            session = client.attach_first_page()
            runlib.wait_ready(client, session, url, 20)
            cookies = client.evaluate(session, "document.cookie")
            # Closing the last page ends lrb, as the window's close button
            # does (lrb has no Browser.close).
            target = next(t for t in client.call("Target.getTargets")["targetInfos"]
                          if t["type"] == "page")
            client.call("Target.closeTarget", {"targetId": target["targetId"]})
            deadline = time.monotonic() + 20
            while proc.poll() is None and time.monotonic() < deadline:
                time.sleep(0.2)
            check(f"start {n} closes cleanly", proc.poll() == 0, f"exit {proc.poll()}")
            return cookies
        finally:
            if client:
                client.close()
            runlib.stop(proc, None)

    try:
        first = visit(base + "/set", 1)
        check("the page set both cookies", "kept=1" in first and "session=1" in first, first)
        check("a cookie database in the profile",
              os.path.exists(os.path.join(work, "p", "Cookies")))
        second = visit(base + "/", 2)
        check("the persistent cookie is back after a restart", "kept=1" in second, second)
        check("the session cookie is not", "session=1" not in second, second)
    finally:
        server.shutdown()
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
