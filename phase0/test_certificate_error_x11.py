"""Check lrb's question about an invalid certificate, on a real display.

  python3 test_certificate_error_x11.py <path to lrb>

Opens a window on the X11 display ($DISPLAY; XWayland works) for about half
a minute. A local HTTPS server with a self-signed certificate (an authority
nobody trusts) stands in for a site with a bad one; a plain HTTP page on the
same host is where the window starts. Keys go to lrb's window only.

1. The page with the bad certificate asks; focus is on Back, so Enter goes
   back to the page before.
2. Asked again (Back isn't remembered): Tab, Space (Continue anyway) loads
   it, its image from the same host too (the site remembers the
   certificate: LrbSslHostState).
3. Visited again: no question.

By hand (NEXT.md): a site that requires HTTPS (HSTS) offers only Back, and
a Wi-Fi network that wants a sign-in offers "Sign in to Wi-Fi".
"""

import http.server
import os
import ssl
import subprocess
import sys
import tempfile
import threading
import time

from lrb_harness import cdp as cdplib
from lrb_harness import run as runlib
from lrb_harness import x11

XK_Return, XK_Tab, XK_space = 0xff0d, 0xff09, 0x20
# A 1x1 PNG.
PIXEL = bytes.fromhex(
    "89504e470d0a1a0a0000000d4948445200000001000000010806000000"
    "1f15c4890000000d49444154789c6360000002000154a24f5d0000000049454e44ae426082")


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        if self.path == "/pixel.png":
            self.send_header("Content-Type", "image/png")
            self.end_headers()
            self.wfile.write(PIXEL)
            return
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        title = "start" if self.server.server_port == START_PORT[0] else "bad certificate"
        self.wfile.write(f'<title>{title}</title><img id=i src="/pixel.png">'.encode())

    def log_message(self, *args):
        pass


START_PORT = [0]


def main():
    binary = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="cert-error-", dir=runlib.ROOT + "/.profiles")
    key, cert = os.path.join(work, "key.pem"), os.path.join(work, "cert.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", key, "-out", cert, "-days", "2", "-subj", "/CN=127.0.0.1",
                    "-addext", "subjectAltName=IP:127.0.0.1"],
                   check=True, capture_output=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    START_PORT[0] = plain.server_address[1]
    secure = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    secure.socket = context.wrap_socket(secure.socket, server_side=True)
    for server in (plain, secure):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    start_url = f"http://127.0.0.1:{plain.server_address[1]}/"
    bad_url = f"https://127.0.0.1:{secure.server_address[1]}/"
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))

    log = os.path.join(work, "log")
    proc = subprocess.Popen(
        [binary, f"--user-data-dir={work}/profile", "--lrb-site=127.0.0.1",
         "--ozone-platform=x11", "--remote-debugging-port=0", "--single-process",
         "--no-zygote", "--no-sandbox", start_url],
        stdout=open(log, "wb"), stderr=subprocess.STDOUT,
        env=dict(os.environ, DBUS_SESSION_BUS_ADDRESS="disabled:"),
        start_new_session=True)

    def keys(*keysyms):
        window = x11.windows()[0][0]
        for keysym in keysyms:
            x11.key(window, keysym, 0)
            time.sleep(0.3)

    def page(client, session):
        """(title, URL, whether its image loaded), once the document settles."""
        time.sleep(2)
        try:
            return tuple(client.evaluate(session, "[document.title, location.href, "
                                                  "!!(document.images[0] && "
                                                  "document.images[0].naturalWidth)]"))
        except cdplib.CDPError:
            return (None, None, False)

    try:
        client = cdplib.CDP(runlib.wait_for_devtools(proc, log))
        session = client.attach_first_page()
        runlib.wait_ready(client, session, start_url, 20)
        client.call("Emulation.setFocusEmulationEnabled", {"enabled": True}, session)
        time.sleep(1)

        # The probe for a Wi-Fi sign-in takes up to 3 s before the question.
        client.call("Page.navigate", {"url": bad_url}, session)
        time.sleep(4)
        check("the page with a bad certificate doesn't load before an answer",
              page(client, session)[0] != "bad certificate")
        keys(XK_Return)
        title, url, _ = page(client, session)
        check("Enter (focus on Back) goes back to the page before",
              title == "start" and url == start_url, f"{title} {url}")

        client.call("Page.navigate", {"url": bad_url}, session)
        time.sleep(4)
        keys(XK_Tab, XK_space)
        title, url, image = page(client, session)
        check("Tab, Space (Continue anyway) loads it", title == "bad certificate",
              f"{title} {url}")
        check("...with its image from the same host",
              title == "bad certificate" and image)

        client.call("Page.navigate", {"url": start_url}, session)
        runlib.wait_ready(client, session, start_url, 10)
        client.call("Page.navigate", {"url": bad_url}, session)
        title, _, _ = page(client, session)
        check("visited again, it loads without a question", title == "bad certificate",
              title)
    finally:
        runlib.stop(proc, None)
        plain.shutdown()
        secure.shutdown()
    print(f"{failures} failure(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
