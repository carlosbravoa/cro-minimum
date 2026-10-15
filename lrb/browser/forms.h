// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_BROWSER_FORMS_H_
#define LRB_BROWSER_FORMS_H_

#include "lrb/common/forms.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace lrb {

// Saved passwords and autofill details for a page: what the page's forms
// (lrb/renderer/forms_agent.h) report and ask for. Every step is a
// question in the window, never silent:
//
//   - a sign-in submitted: "Save the password for alice on example.com?"
//     (or "Update ..." if alice's is saved and differs);
//   - a sign-in form shown: "Fill in the saved password for alice?"
//     (with several accounts, which one, in a text field);
//   - name, address, email or phone submitted: "Remember your name and
//     address for forms on every site?";
//   - such a form shown: "Fill in your saved name and address?".
//
// Kept in the desktop's keyring by the coordinator (keyring.h): passwords
// per site, details shared by every site. Only for the page's own frame,
// only on http(s), only while its tab is on screen; without a keyring,
// never offered.
void BindForms(content::RenderFrameHost* frame,
               mojo::PendingReceiver<mojom::Forms> receiver);

}  // namespace lrb

#endif  // LRB_BROWSER_FORMS_H_
