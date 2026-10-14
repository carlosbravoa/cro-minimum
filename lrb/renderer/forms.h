// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_RENDERER_FORMS_H_
#define LRB_RENDERER_FORMS_H_

#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_frame_observer.h"
#include "lrb/common/forms.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace lrb {

// A page's sign-in and address forms, for saved passwords and autofill
// (lrb/browser/forms.h, which asks the user every time). In the main frame
// only. Finds:
//   - a sign-in: the first visible password field, and as its user name
//     the field marked autocomplete="username", else the last visible text
//     or email field before it;
//   - details: fields marked with autocomplete tokens lrb keeps (name,
//     email, tel, street-address, postal-code, ...), and email and phone
//     fields.
// Reports them when a form is submitted, or when the page leaves by its
// own navigation with a password typed (sign-ins done by script). Offers
// to fill in once per document, at load and after same-document
// navigations (single-page apps). Fills in with SetAutofillValue, which
// sends the events a page listens for, only after the user said yes.
class FormsAgent : public content::RenderFrameObserver {
 public:
  explicit FormsAgent(content::RenderFrame* render_frame);
  FormsAgent(const FormsAgent&) = delete;
  FormsAgent& operator=(const FormsAgent&) = delete;
  ~FormsAgent() override;

  // content::RenderFrameObserver:
  void DidCommitProvisionalLoad(ui::PageTransition transition) override;
  void DidFinishLoad() override;
  void DidFinishSameDocumentNavigation() override;
  void WillSubmitForm(const blink::WebFormElement& form) override;
  void DidStartNavigation(
      const GURL& url,
      std::optional<blink::WebNavigationType> navigation_type) override;
  void OnDestruct() override;

 private:
  mojom::Forms* Forms();
  // Asks the browser to offer what this document may want filled in.
  void Offer();
  // Reports a typed sign-in and details from the document (or `form`).
  void Report(const blink::WebFormElement* form);
  void FillPassword(const std::optional<std::u16string>& username,
                    const std::optional<std::u16string>& password);
  void FillDetails(
      const std::optional<base::flat_map<std::string, std::u16string>>&
          values);

  mojo::Remote<mojom::Forms> forms_;
  // Per document.
  bool offered_password_ = false;
  bool offered_details_ = false;
  bool reported_ = false;
  std::u16string filled_password_;  // lrb's, not one typed

  base::WeakPtrFactory<FormsAgent> weak_factory_{this};
};

}  // namespace lrb

#endif  // LRB_RENDERER_FORMS_H_
