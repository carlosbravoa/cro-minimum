// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_BROWSER_CERTIFICATE_ERRORS_H_
#define LRB_BROWSER_CERTIFICATE_ERRORS_H_

#include "base/functional/callback_forward.h"
#include "content/public/browser/certificate_request_result_type.h"

class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace lrb {

// A page whose certificate isn't valid (Chrome's rule).
// content_shell refused it with a bare error code. Now the window says what
// is wrong ("example.com's certificate has expired ...") and offers Back
// and Continue anyway, except on sites that require valid HTTPS (HSTS:
// `strict`), which only go back.
//
// First an HTTP probe, as Chrome's captive portal check: a Wi-Fi network
// that intercepts traffic until you sign in (hotels, airports) answers it
// with its own page, and the window offers "Sign in to Wi-Fi" instead,
// which opens that page.
//
// A Continue is remembered by the site's process (LrbSslHostState), so
// the page's other requests to that host pass. Requests that aren't the
// page itself (frames, images of other hosts) are refused without asking,
// as in Chrome: the caller doesn't call this for them.
void AskAboutCertificateError(
    content::WebContents* contents,
    int cert_error,
    const GURL& url,
    bool strict,
    base::OnceCallback<void(content::CertificateRequestResultType)> answer);

}  // namespace lrb

#endif  // LRB_BROWSER_CERTIFICATE_ERRORS_H_
