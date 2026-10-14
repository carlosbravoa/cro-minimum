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
| `158233b` | Links for other apps (`mailto:`, `tel:`, Zoom and Teams links, `magnet:`): the window asks "Open ... in another app?", then the coordinator runs `xdg-open` (`open-app`). The coordinator takes only links for apps (never http(s), files, `data:`, `javascript:`; `IsExternalAppUrl` in `rules.h`, with unit tests) and one every 2 s per instance, since a compromised instance can skip the question. The page itself may ask without a click (Zoom's join page does); frames inside it need one; only the tab on screen asks. `test_open_app.py` checks the coordinator with a stand-in instance (passes with the new coordinator built by g++, fails on alpha.4's); the rules and the coordinator compile cleanly with g++ `-Wall -Wextra` | `lrb/browser/external_links.{h,cc}`, `lrb/browser/lrb_content_browser_client.{h,cc}`, `lrb/coordinator/{coordinator.cc,rules.h,rules.cc,rules_unittest.cc}`, `lrb/BUILD.gn`, `phase0/test_open_app.py`, `phase0/run_tests.sh`, README, `docs/design.md` |
| `d0df6da` | Your own certificates (`~/.pki/nssdb`, shared with Chrome and Firefox): confined sites may read it, so client certificates and authorities you added work, but not write it. Chromium opens it read-write; under Landlock SQLite falls back to reading, so no Chromium patch. `test_confine.py` runs a stand-in instance under the real confinement in a made-up home (passes with the new coordinator, fails on alpha.4's) | `lrb/coordinator/coordinator.cc`, `phase0/test_confine.py`, `phase0/run_tests.sh`, README |

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
5. Links for other apps, by hand: a `mailto:` link on a page asks, and
   Open starts the mail app with the address; Cancel and Esc do nothing;
   Enter right after the click lands on Cancel. A Zoom join page
   (`zoom.us/j/<id>`) asks without a click. A `tel:` link in a background
   tab asks nothing. A page that keeps opening `mailto:` shows one
   question, not a stack. With no app for a scheme, xdg-open fails
   quietly: maybe say so in the window. `lrb_unittests` passes.
6. Your own certificates: with a client certificate in `~/.pki/nssdb`
   (or a company authority), a site that asks for it gets it; no "Error
   initializing NSS" in the log.
7. `python3 -m lrb_harness run --configs lrb-lean-pressure` against
   alpha.4: unreclaimable memory should drop on sites with many cached
   resources (the cache left RAM); the title timer and the databases
   shouldn't cost anything measurable. Also a window on Wayland against
   X11 (`--display native`, `--extra-args=--ozone-platform=wayland`).
   Add the results to MEASUREMENTS.md.

## Missing for everyday use

Found 2026-10-14 by looking for what content_shell leaves out and lrb
never added back. In order (dead keys, links for other apps and your
own certificates are in the next build):

1. **No security updates** (deferred 2026-10-14 until lrb has a
   distribution channel). Every Chromium security release (about every 4
   weeks, plus emergency fixes) reaches users only by reinstalling. At
   least: lrb notices a newer release and says so.
2. **Certificate errors are a dead end**: an error page, no explanation,
   no captive-portal detection, so hotel and airport Wi-Fi sign-in pages
   can't be reached when they intercept HTTPS.
3. **No saved passwords.** Signing in is typed every time (less often
   with cookies kept). Chrome's password manager is large; a small one
   (per site, in the site's profile, filled on request) is real work.
4. **No form autofill** (names, addresses).
5. **No spellcheck.** A few MB per dictionary: off by default, or one
   language, and measured.
6. **No Safe Browsing** (phishing and malware warnings). The blocking
   lists catch some malware domains, not phishing pages.
7. **Check, likely fine:** the color and date pickers (`<input
   type=color|date>`) open under X11 (Blink's own popups; checked on
   alpha.4) — check them on Wayland, now the default. A screen reader:
   lrb links ATK and AT-SPI (not GTK's), so Orca may work; check it,
   confined (the accessibility bus is under /run, readable).

Decided, not missing (README, "What it leaves out"): extensions, sync,
DRM (Netflix, Spotify), the PDF viewer (PDFs download), notifications and
location (refused), screen sharing (refused: video calls can't present
from the browser), translation (2026-10-14: it would send pages' text to
a service), automatic updates (deferred until there's a distribution
channel).

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
