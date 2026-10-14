# Next

## The next build

Committed but not built yet (after v0.1.0-alpha.4). None of it has been
through the compiler, so the build is its first check.

| Commit | Change | Files |
|---|---|---|
| `70b0d56` | The site's memory in the window title: `Linux - Wikipedia — 259 MB`, the process's PSS read every 5 s on the thread pool; titles change only when the whole-MB figure does | `lrb/browser/ui/lrb_platform_delegate.{h,cc}`, `phase0/lrb_harness/x11.py`, README |
| `d0ec635` | Cookies kept across restarts: each site's cookies in a `Cookies` database in its profile (content_shell's store was in memory only, so every site was signed out at each start; reported with Facebook). Unencrypted: Chromium 155 hits `NOTREACHED` without a key provider | `lrb/browser/lrb_content_browser_client.cc`, `phase0/test_cookies.py`, `phase0/run_tests.sh` |
| `553108b` | The HTTP cache on disk (`Cache`, 32 MB) instead of in memory, where it held up to 2% of RAM per site (50 MB from 2.5 GB up) and started empty each time; HSTS remembered (`TransportSecurity`). `test_cookies.py` becomes `test_persistence.py` and checks the cache too | `lrb/browser/lrb_content_browser_client.cc`, `phase0/test_persistence.py`, `phase0/run_tests.sh` |
| `75b2c46` | Native Wayland windows on a Wayland desktop (the coordinator adds `--ozone-platform=wayland` when `WAYLAND_DISPLAY` is set and no platform was given): through Xwayland without GTK, dead keys and input methods didn't work, so `á`, `ü` couldn't be typed. Confirmed by hand on alpha.4 with `low-ram-browser -- --ozone-platform=wayland`. The X11 tests started through the coordinator now ask for x11; the desktop entry gets `StartupWMClass=lrb` | `lrb/coordinator/coordinator.cc`, `phase0/test_confined_window_x11.py`, `phase0/test_bookmarks_x11.py`, `build/release/install.sh` |

Check before releasing:

1. It compiles.
2. `phase0/run_tests.sh` on the build: `test_persistence.py` passes (it
   fails on alpha.4, as it should), and the X11 tests that compare window
   titles (`test_back_x11.py`, `test_tabs_x11.py`) still pass; their title
   reader strips the memory suffix.
3. By hand: sign in to a site (Facebook, "trust this device"), close lrb,
   start it again: still signed in. The title shows the MB figure and
   follows page title changes and tab switches. A site that sends HSTS
   (not on the preload list) has a `TransportSecurity` file in its
   profile after a visit.
4. On a Wayland desktop, from the applications menu: `á é ñ ü ¿ ¡`
   type; an input method works if one is set up. Then what Wayland
   changes from X11: a window replacing another site's keeps its size but
   the compositor places it (Wayland clients can't position windows); the
   coordinator raising a site's existing window (GNOME may show "is ready"
   instead); the dock icon and grouping (app_id `lrb`, `StartupWMClass`);
   the window icon (the site's favicon was set through X11); the file
   picker over its window; GPU rendering smoothness (1-8 fps under
   Xwayland, see Open). On a Raspberry Pi with labwc too.
5. `python3 -m lrb_harness run --configs lrb-lean-pressure` against
   alpha.4: unreclaimable memory should drop on sites with many cached
   resources (the cache left RAM); the title timer and the databases
   shouldn't cost anything measurable. Also a window on Wayland against
   X11 (`--display native`, `--extra-args=--ozone-platform=wayland`).
   Add the results to MEASUREMENTS.md.

## Missing for everyday use

Found 2026-10-14 by looking for what content_shell leaves out and lrb
never added back. In order (dead keys, the first, is in the next build):

1. **Links to other apps do nothing**: `mailto:`, `tel:`, Zoom and Teams
   meeting links, `magnet:`. content_shell ignores external protocols.
   Hand them to the desktop's default app (the OpenURI portal, through the
   coordinator, which already shows the file picker), after asking.
2. **No security updates.** Every Chromium security release (about every
   4 weeks, plus emergency fixes) reaches users only by reinstalling. At
   least: lrb notices a newer release and says so.
3. **Certificate errors are a dead end**: an error page, no explanation,
   no captive-portal detection, so hotel and airport Wi-Fi sign-in pages
   can't be reached when they intercept HTTPS.
4. **No saved passwords.** Signing in is typed every time (less often
   with cookies kept). Chrome's password manager is large; a small one
   (per site, in the site's profile, filled on request) is real work.
5. **No form autofill** (names, addresses).
6. **No spellcheck.** A few MB per dictionary: off by default, or one
   language, and measured.
7. **No Safe Browsing** (phishing and malware warnings). The blocking
   lists catch some malware domains, not phishing pages.
8. **Certificates you imported yourself aren't seen**: the site's process
   can't open `~/.pki/nssdb` under confinement ("Error initializing NSS
   with a persistent database"), so client certificates (some company and
   government sign-ins) and company certificate authorities don't work.
9. Smaller: no color picker (`<input type=color>`), no translation, no
   screen-reader support (no ATK without GTK).

Decided, not missing (README): extensions, DRM (Netflix, Spotify), the
PDF viewer (PDFs download), notifications and location (refused), screen
sharing (refused: video calls can't present from the browser), sync.

## Open

- **README's "Against Chromium" table has no YouTube row:** Google
  answered the test machine with its "unusual traffic" page on
  2026-10-14. Measure it again with `tools/compare_chromium.py` later.
- **GPU rendering scrolls at 1-8 fps** on an Intel UHD 630 under GNOME
  Wayland (X11 through Xwayland), for lrb and Chromium alike; software
  compositing gives 120 fps there. The settings window's first-start
  question recommends the GPU wherever `/dev/dri/renderD*` exists: it
  might have to measure, or default to software.
- **The program name** GLib reports is odd under Wayland
  (`(?q=facebook:<pid>)` in its warnings, where `lrb` was expected): see
  where it comes from, in case it reaches the app_id or the dock.
- **Session cookies** still end with the site's process, even when lrb
  restores its windows after a crash or a power cut. Chrome persists them
  when it restores the session. Decide whether lrb should (a restored
  window would stay signed in on sites that use session cookies), and how
  that fits "closing a site's windows yourself forgets them".
