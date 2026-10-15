# Design

## Goal

The smallest possible memory footprint while mainstream sites keep working.
Every choice is judged by measured memory, with compatibility as the hard
constraint.

The point is a browser that works whenever RAM is tight, not just on old
hardware. That includes a machine where other programs hold most of the
memory, so the browser must react to pressure as it happens, not only fit a
fixed budget. Targets: 2 GB x86-64 laptops (primary) and a 1 GB Raspberry Pi
(arm64).

## How it works

1. **`//content`, not `//chrome`.** The Chrome product layer (sync,
   extensions, Safe Browsing, profile services) costs about 70 MB of
   anonymous memory over content_shell for the same page.
2. **One site per window, one browser process per window.** A window shows
   one site (registrable domain: `google.com` covers `mail.google.com`), and
   each window is its own single-process browser instance with its own
   persistent profile. Users don't have to understand process sharing to be
   safe: sites never share a process or a cookie jar. Five sites cost 32%
   less than Chrome's model.
3. **Single process per instance.** The renderer, compositor, network,
   audio and storage all run in the instance's one process, without zygotes.
4. **Tabs within a site.** Every new page of a site opens as a tab in that
   site's most recently used window: New page, links asking for a new tab,
   and the site opened from elsewhere. Popups that need their opener
   (sign-in, payment) stay small separate windows. A tab opened in the
   background loads only when first shown.
5. **Leaving a site replaces the window.** Typing another site's address,
   or a plain link click to another site, hands the window to that site and
   the old instance exits if it was the last. Links asking for a new tab
   open the other site's window and keep this one. Redirects within a flow
   (OAuth, 3-D Secure, checkout) may pass through other sites in the
   current window: they run in that window's process and profile, so they
   can't reach any other site's data. Back from a replaced window's first
   page returns to the site it left, restored with its history.
6. **Memory pressure.** Background tabs, then background windows, are
   discarded in place under pressure (window and history kept, reloaded
   when shown). Only as a last resort, still critical with every background
   window discarded, do the oldest windows close; opening the site again
   restores them with their history. Chromium has no memory pressure
   source on Linux; `patches/0001` adds one.
7. **A coordinator** (`lrb_coordinator`): a tiny separate process that never
   loads web content. It maps sites to instances, launches them, and
   discards or closes them under pressure. A hijacked page can't steer
   which windows open, which profiles they get, or what is discarded.
8. **Content blocking** uses Brave's adblock-rust (uBlock Origin-compatible
   lists: surrogates, cosmetic rules, scriptlets), so blocking stays as
   invisible to sites as the community lists keep it. The compiled engine
   is one read-only file shared by every instance: 0.8 MB private per
   instance. Phishing and malware warnings (in place of Safe Browsing)
   use the same engine on public lists of phishing pages and malware
   downloads (malware-filter, uBlock Origin's badware), compiled into a
   file of their own so turning blocking off never turns them off: a
   listed page or download is held before its request
   (`lrb/browser/safety_throttle.h`), with Back and Continue anyway. The
   coordinator refreshes the lists when it starts, never while it runs.
   0.85 MB private per instance; nothing about the pages visited leaves
   the machine.
9. **Window UI:** a slim native (Views) bar with back, forward, reload, a
   site chip in the site's colour with the path beside it (click to edit),
   a blocked-request count and a menu; a second thin row for tabs with two
   or more pages. Not HTML: 6.5 MB against about 2 MB per window.
10. **Permissions:** none without the user saying yes. Camera, microphone
    and clipboard reading are asked about in the window, remembered per
    site and revocable from the menu; everything else is refused.
    Downloads ask where to save.
11. **Search:** DuckDuckGo's HTML page by default, changeable in the
    settings.
12. **GPU:** the user chooses. On first start, on a machine with a GPU,
    lrb shows the memory trade-off (7-55 MB more per window on a
    Raspberry Pi 3, about 12 MB on a desktop with NVIDIA) and asks,
    suggesting neither: smoothness hasn't been measured yet. Changeable in
    Settings. Unanswered (or without a display), software rendering.
13. **Chromium release line:** Stable (what Linux distributions ship),
    rebuilt for each security release; never main or canary.
    `build/update_stable.sh` automates it, including a memory comparison
    against the previous release. Chromium's Extended Stable has no Linux
    releases.
14. **Own copy of content_shell.** lrb started from content_shell; the parts
    it uses are copied into `//lrb` and trimmed of web tests, test hooks and
    other platforms. Only genuine Chromium changes live in `patches/`, which
    keeps Stable updates cheap.

## Security model

| Threat | Chrome | lrb |
|---|---|---|
| A site reading another site's data (cookies, storage) | Blocked: data in a privileged process, renderers locked to their site | Blocked: other sites' data is in other processes and profiles |
| A hijacked page reaching your files and system | Per-renderer sandbox | Whole-instance OS confinement (Landlock, seccomp, no_new_privs; `lrb/coordinator/confine.h`) and the V8 sandbox. The user's files reach it only through the coordinator's file picker (`broker.h`) |
| Third-party content (ads, embeds) inside a page | Cross-site iframes in their own process | **Shares the page's process**: a malicious ad on a site can reach that site's data. Mitigated by content blocking |
| One page crashing others | Only its renderer dies | Only its window's instance dies |
| A site learning which other sites you use | History and bookmarks in the browser process, out of renderers' reach | **Readable by any instance** through the coordinator (address-bar suggestions, the bookmarks menu): the sites visited and bookmarks, not what was done there. An instance can add or remove bookmarks of its own site only |
| A hijacked page opening other apps (`mailto:`, `tel:`, meeting links) | Asked in the browser process, out of the renderer's reach | **Asked in the site's instance**, which a hijacked page could skip; the coordinator lets through only links for apps (never web pages or files, `rules.h`), one every 2 s (`lrb/coordinator/coordinator.cc`, OpenInApp) |
| A phishing page or a malware download | Google Safe Browsing: lists on the device, refreshed every 30 minutes, and a check with Google for addresses that match | **Public lists, checked on the device** (malware-filter's phishing and URLhaus lists, uBlock Origin's badware), refreshed when lrb starts: fewer pages caught, and new ones only from the next start. Warned in the site's instance, as certificate errors are |
| A site reading saved passwords and autofill details | In the browser process; filled in by it | **In the desktop's keyring**, through the coordinator (`lrb/coordinator/keyring_broker.h`): an instance gets passwords of its own site only (from the profile the coordinator gave it), never others'. Autofill details (name, address, email, phone) are shared by every site: a hijacked instance can read them without asking |

The remaining exposure is third-party frames inside a site's own window.
Banks and email providers carry few, and content blocking removes most of
the rest.

The coordinator's protocol (`lrb/coordinator/coordinator.cc`) is text lines
over a Unix socket in `$XDG_RUNTIME_DIR/lrb` (mode 0700). It checks that
every URL belongs to the named site and that site names are safe directory
names. A compromised instance can open windows for any site (as a click
could), but it can't make another site's window load a foreign URL or touch
files outside the profiles directory. Bookmark changes are tied to the
profile the coordinator started the instance with (`SO_PEERCRED`), never to
what it says about itself (`lrb/coordinator/bookmarks.h`).

Printing is compiled in for Print to PDF only (`enable_printing`, no print
preview or CUPS; `lrb/browser/print.h`): a page's `window.print()` reaches
Chromium's printing code in the renderer (PrintRenderFrameHelper, Skia's
PDF backend), as in Chrome.
