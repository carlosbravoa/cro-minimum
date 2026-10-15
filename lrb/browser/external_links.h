// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_BROWSER_EXTERNAL_LINKS_H_
#define LRB_BROWSER_EXTERNAL_LINKS_H_

class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace lrb {

// Links for other apps: mailto:, tel:, zoommtg:, magnet: and the like.
// content_shell dropped them, so "email us" and "join meeting" did
// nothing. The window asks first ("Open ... in another
// app?"), as Chrome does; then the coordinator hands the link to the
// desktop (xdg-open): a confined instance can't start programs. Run on its
// own, without a coordinator, lrb runs xdg-open itself.
//
// `clicked`: the link follows a click. `from_page`: the page's own frame
// asked (not a frame inside it, nor a sandboxed one). The page may ask
// without a click (Zoom's join page opens its app by itself); frames inside
// it (ads, embeds) need one. Only the tab on screen asks, one question per
// page at a time; the rest is dropped.
void AskToOpenInApp(content::WebContents* contents,
                    const GURL& url,
                    bool clicked,
                    bool from_page);

}  // namespace lrb

#endif  // LRB_BROWSER_EXTERNAL_LINKS_H_
