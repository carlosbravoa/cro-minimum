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
| `50ba59a` | Certificate errors: the window says what's wrong (expired or a wrong clock, another site's, an untrusted authority, revoked, weak) with Back (focused) and Continue anyway, only Back on HSTS sites; a probe (`generate_204`, as Chrome's) first, and on a network that intercepts traffic "Sign in to Wi-Fi" instead. A Continue is remembered in memory for the site's process (`LrbSslHostState`, content_shell had none), so the page's other requests to the host pass. Frames and other hosts' requests are refused without asking. `test_certificate_error_x11.py` (self-signed local HTTPS; fails on alpha.4, as it should) | `lrb/browser/certificate_errors.{h,cc}`, `lrb/browser/ssl_host_state.{h,cc}`, `lrb/browser/lrb_browser_context.{h,cc}`, `lrb/browser/lrb_content_browser_client.{h,cc}`, `lrb/BUILD.gn`, `phase0/test_certificate_error_x11.py`, `phase0/run_tests.sh`, README |
| `88ffb93` | Keyring back end for passwords and autofill: `lrb_keyring` (Secret Service through libsecret loaded at run time, Chromium's vendored headers; secrets only on stdin/stdout) and the coordinator's `KeyringBroker` (`pw-*`, `af-*`): passwords only of the instance's own site (from its profile) for origins on it, autofill one record for every site. `test_keyring_broker.py` passes with a stand-in and with the real helper against a throwaway GNOME keyring | `lrb/coordinator/keyring{,_broker}.{h,cc}`, `lrb/coordinator/coordinator.cc`, `lrb/BUILD.gn`, `build/build.sh`, `build/package_release.sh`, `phase0/test_keyring_broker.py` |
| `c3d6ca5` | Saved passwords and autofill in pages: `lrb/common/forms.mojom`; the page side (`lrb/renderer/forms.{h,cc}`: sign-in and detail fields by autocomplete tokens, reported on submit or when the page leaves by itself, filled with `SetAutofillValue` after a yes) and the window's questions (`lrb/browser/forms.{h,cc}`, `lrb/browser/keyring.{h,cc}`): save or update, fill in (several accounts: which, in the row's text field), remember details, fill in details. Every value capped to fit the coordinator's 9000-byte lines | `lrb/common/forms.mojom`, `lrb/renderer/forms.{h,cc}`, `lrb/browser/forms.{h,cc}`, `lrb/browser/keyring.{h,cc}`, `lrb/browser/lrb_content_browser_client.cc`, `lrb/renderer/lrb_content_renderer_client.cc`, `lrb/BUILD.gn`, README, `docs/design.md` |
| `bd647bc` | Phishing and malware warnings (in place of Safe Browsing): malware-filter's phishing and URLhaus lists and uBlock Origin's badware, compiled by the updater (`--lrb-adblock-setting=safety`) into `safety.adb`, apart from the ad lists; bare hostnames become `\|\|host^$all`. A listed page, redirect or download is held before its request (`SafetyThrottle`): Back (focused) or Continue anyway, remembered per host until the instance exits. The coordinator refreshes the file once when it starts, if over an hour old, after the filter lists, never while it runs. 0.85 MB private per instance, measured with the pinned adblock-rust outside Chromium. `test_safety_lists.py` (coordinator, stand-in) and `test_confine.py` (the file readable, not writable) pass with the g++ coordinator; `test_safety_warning_x11.py` is for the build | `lrb/adblock/lib.rs`, `lrb/common/{safety_list,content_blocker}.{h,cc}`, `lrb/common/lrb_switches.h`, `lrb/browser/safety_throttle.{h,cc}`, `lrb/browser/list_updater.{h,cc}`, `lrb/browser/lrb_browser_main_parts.cc`, `lrb/browser/lrb_content_browser_client.cc`, `lrb/app/lrb_main_delegate.cc`, `lrb/coordinator/coordinator.cc`, `lrb/BUILD.gn`, `phase0/` (tests get `--safety-lists=off`), README, `docs/design.md`, THIRD_PARTY.md |

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
7. Certificate errors, by hand: `expired.badssl.com`, `wrong.host.badssl.com`,
   `self-signed.badssl.com` each say what's wrong; Continue loads the
   page with its images; a second visit doesn't ask. An HSTS site with a
   bad certificate offers only Back (`subdomain.preloaded-hsts.badssl.com`).
   On a Wi-Fi network that wants a sign-in (or with the probe URL
   redirected), "Sign in to Wi-Fi" opens its page. On a Pi with the clock
   wrong (no RTC, before NTP), the message names the clock.
8. Saved passwords, by hand, on a desktop with a keyring: sign in to a
   site (a form that submits, and one that signs in by script, as
   GitHub's): "Save the password for alice on ...?"; Save puts an
   "lrb: password for https://..." item in Seahorse. Next visit: "Fill in
   the saved password for alice?" fills both fields and the page accepts
   them (React and the like see the events). Two accounts: the row's
   field picks one. Changing the password: "Update ...". A sign-up form
   saves the new password, a change-password form doesn't fill the new
   field. Without a keyring (`DBUS_SESSION_BUS_ADDRESS=disabled:`
   for the coordinator), nothing is offered. Whether the questions,
   which don't take focus, can be reached by keyboard (Tab from the page?
   F6?): if not, they need a way. Then an X11 test like
   test_certificate_error_x11.py, in a dbus-run-session with a throwaway
   gnome-keyring (test_keyring_broker.py shows how).
9. Autofill, by hand: a checkout form with name, email and address
   (autocomplete tokens): submitting it asks "Remember your name, email
   and address ...?"; another site's form then offers "Fill in your saved
   ...". Fields already typed in aren't overwritten.
10. Phishing and malware warnings, by hand: the first start fetches the
   lists (`updating safety lists` in the coordinator's log, `safety.adb`
   of about 7.5 MB in `~/.local/share/lrb/adblock`); a window opened
   after that warns on a page from today's phishing list (pick one from
   https://malware-filter.gitlab.io/malware-filter/phishing-filter.txt),
   and on a site with content blocking turned off too. Restarting within
   the hour fetches nothing. Then measure the instance's cost with the
   file against without it (the 0.85 MB was measured outside Chromium).
11. `python3 -m lrb_harness run --configs lrb-lean-pressure` against
   alpha.4: unreclaimable memory should drop on sites with many cached
   resources (the cache left RAM); the title timer and the databases
   shouldn't cost anything measurable. Also a window on Wayland against
   X11 (`--display native`, `--extra-args=--ozone-platform=wayland`).
   Add the results to MEASUREMENTS.md.

## Missing for everyday use

Found 2026-10-14 by looking for what content_shell leaves out and lrb
never added back. In order (dead keys, links for other apps, your own
certificates, certificate errors, saved passwords and autofill, phishing
and malware warnings are in the next build):

1. **No security updates** (deferred 2026-10-14 until lrb has a
   distribution channel). Every Chromium security release (about every 4
   weeks, plus emergency fixes) reaches users only by reinstalling. At
   least: lrb notices a newer release and says so.
2. **No spellcheck.** A few MB per dictionary: off by default, or one
   language, and measured.
3. **Check, likely fine:** the color and date pickers (`<input
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
