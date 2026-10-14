// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/renderer/forms.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/mojom/forms/form_control_type.mojom-shared.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_form_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_navigation_type.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace lrb {

namespace {

using blink::WebFormControlElement;
using blink::mojom::FormControlType;

// Forms that appear after the page loaded (drawn by script) get a second
// look this much later.
constexpr base::TimeDelta kLateFormsDelay = base::Seconds(2);

// Autocomplete tokens of details lrb keeps (lrb/browser/forms.cc says them
// in words). Not cards, passwords or one-time codes.
constexpr const char* kDetailTokens[] = {
    "name",         "given-name",     "additional-name", "family-name",
    "nickname",     "honorific-prefix", "honorific-suffix", "email",
    "tel",          "tel-national",   "organization",    "street-address",
    "address-line1", "address-line2", "address-line3",   "address-level1",
    "address-level2", "address-level3", "postal-code",   "country",
    "country-name"};

std::vector<WebFormControlElement> Controls(const blink::WebDocument& document,
                                            const blink::WebFormElement* form) {
  if (form) {
    return form->GetFormControlElements();
  }
  std::vector<WebFormControlElement> controls;
  for (const blink::WebFormElement& each : document.GetOutermostForms()) {
    std::vector<WebFormControlElement> inside = each.GetFormControlElements();
    controls.insert(controls.end(), inside.begin(), inside.end());
  }
  std::vector<WebFormControlElement> loose =
      document.UnassociatedFormControls();
  controls.insert(controls.end(), loose.begin(), loose.end());
  return controls;
}

bool Usable(const WebFormControlElement& control) {
  return control.IsEnabled() && !control.IsReadOnly() &&
         !control.BoundsInWidget().IsEmpty();
}

bool IsPassword(const WebFormControlElement& control) {
  return control.FormControlType() == FormControlType::kInputPassword;
}

bool IsTextLike(const WebFormControlElement& control) {
  switch (control.FormControlType()) {
    case FormControlType::kInputText:
    case FormControlType::kInputEmail:
    case FormControlType::kInputTelephone:
    case FormControlType::kTextArea:
    case FormControlType::kSelectOne:
      return true;
    default:
      return false;
  }
}

// The field's autocomplete token ("email", "current-password"): the last
// word of its autocomplete attribute; for email and phone fields without
// one, their type.
std::string Token(const WebFormControlElement& control) {
  const std::string attribute =
      base::ToLowerASCII(control.GetAttribute("autocomplete").Utf8());
  std::vector<std::string> words = base::SplitString(
      attribute, " \t\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (!words.empty() && words.back() != "on" && words.back() != "off") {
    return words.back();
  }
  switch (control.FormControlType()) {
    case FormControlType::kInputEmail:
      return "email";
    case FormControlType::kInputTelephone:
      return "tel";
    default:
      return std::string();
  }
}

struct SignIn {
  WebFormControlElement user;  // may be null
  WebFormControlElement password;
};

// The sign-in among `controls`. To fill in: the current password field (a
// change-password form's first). To save: the new password, if one was
// typed (sign-up and change-password forms), else the one typed.
std::optional<SignIn> FindSignIn(
    const std::vector<WebFormControlElement>& controls,
    bool to_save) {
  std::optional<size_t> found;
  for (size_t i = 0; i < controls.size(); ++i) {
    const WebFormControlElement& control = controls[i];
    if (!IsPassword(control) || !Usable(control)) {
      continue;
    }
    const std::string token = Token(control);
    if (to_save) {
      if (control.Value().IsEmpty()) {
        continue;
      }
      if (token == "new-password" || !found) {
        found = i;
      }
    } else if (token != "new-password") {
      found = i;
      break;
    }
  }
  if (!found) {
    return std::nullopt;
  }
  SignIn sign_in;
  sign_in.password = controls[*found];
  for (const WebFormControlElement& control : controls) {
    if (Usable(control) && Token(control) == "username") {
      sign_in.user = control;
      return sign_in;
    }
  }
  for (size_t i = *found; i-- > 0;) {
    const WebFormControlElement& control = controls[i];
    if (Usable(control) && !IsPassword(control) &&
        (control.FormControlType() == FormControlType::kInputText ||
         control.FormControlType() == FormControlType::kInputEmail)) {
      sign_in.user = control;
      break;
    }
  }
  return sign_in;
}

// Detail fields among `controls`, by token (the first of each).
std::map<std::string, WebFormControlElement> FindDetails(
    const std::vector<WebFormControlElement>& controls) {
  std::map<std::string, WebFormControlElement> details;
  for (const WebFormControlElement& control : controls) {
    if (!Usable(control) || IsPassword(control) || !IsTextLike(control)) {
      continue;
    }
    const std::string token = Token(control);
    if (std::ranges::find(kDetailTokens, token) != std::end(kDetailTokens) &&
        !details.contains(token)) {
      details.emplace(token, control);
    }
  }
  return details;
}

}  // namespace

FormsAgent::FormsAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

FormsAgent::~FormsAgent() = default;

mojom::Forms* FormsAgent::Forms() {
  if (!forms_.is_bound()) {
    render_frame()->GetBrowserInterfaceBroker().GetInterface(
        forms_.BindNewPipeAndPassReceiver());
  }
  return forms_.get();
}

void FormsAgent::DidCommitProvisionalLoad(ui::PageTransition transition) {
  // A new document: the browser's side is per document too.
  forms_.reset();
  offered_password_ = offered_details_ = reported_ = false;
  filled_password_.clear();
  weak_factory_.InvalidateWeakPtrs();  // answers for the old one
}

void FormsAgent::DidFinishLoad() {
  Offer();
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&FormsAgent::Offer, weak_factory_.GetWeakPtr()),
      kLateFormsDelay);
}

void FormsAgent::DidFinishSameDocumentNavigation() {
  // A single-page app moved on: a sign-in done by script may be in the
  // fields still; then whatever form it shows now.
  Report(nullptr);
  Offer();
}

void FormsAgent::WillSubmitForm(const blink::WebFormElement& form) {
  Report(&form);
}

void FormsAgent::DidStartNavigation(
    const GURL& url,
    std::optional<blink::WebNavigationType> navigation_type) {
  // Left by the page itself (a script after its sign-in), not by the user
  // typing an address or going back.
  if (navigation_type.has_value()) {
    Report(nullptr);
  }
}

void FormsAgent::OnDestruct() {
  delete this;
}

void FormsAgent::Offer() {
  const std::vector<WebFormControlElement> controls =
      Controls(render_frame()->GetWebFrame()->GetDocument(), nullptr);
  if (!offered_password_ && FindSignIn(controls, /*to_save=*/false)) {
    offered_password_ = true;
    Forms()->OfferPassword(
        base::BindOnce(&FormsAgent::FillPassword, weak_factory_.GetWeakPtr()));
  }
  if (!offered_details_) {
    const std::map<std::string, WebFormControlElement> details =
        FindDetails(controls);
    if (details.size() >= 2) {  // one alone is a search or newsletter box
      offered_details_ = true;
      std::vector<std::string> tokens;
      for (const auto& [token, control] : details) {
        tokens.push_back(token);
      }
      Forms()->OfferDetails(tokens, base::BindOnce(&FormsAgent::FillDetails,
                                                   weak_factory_.GetWeakPtr()));
    }
  }
}

void FormsAgent::Report(const blink::WebFormElement* form) {
  if (reported_) {
    return;
  }
  const std::vector<WebFormControlElement> controls =
      Controls(render_frame()->GetWebFrame()->GetDocument(), form);
  if (std::optional<SignIn> sign_in = FindSignIn(controls, /*to_save=*/true);
      sign_in && !sign_in->user.IsNull()) {
    const std::u16string user = sign_in->user.Value().Utf16();
    const std::u16string password = sign_in->password.Value().Utf16();
    // Filled in by lrb and unchanged: nothing new to save.
    if (!user.empty() && !password.empty() && password != filled_password_) {
      Forms()->PasswordSubmitted(user, password);
      reported_ = true;
    }
  }
  base::flat_map<std::string, std::u16string> values;
  for (const auto& [token, control] : FindDetails(controls)) {
    if (!control.Value().IsEmpty()) {
      values[token] = control.Value().Utf16();
    }
  }
  if (values.size() >= 2) {
    Forms()->DetailsSubmitted(values);
    reported_ = true;
  }
}

void FormsAgent::FillPassword(const std::optional<std::u16string>& username,
                              const std::optional<std::u16string>& password) {
  if (!password) {
    return;
  }
  std::optional<SignIn> sign_in = FindSignIn(
      Controls(render_frame()->GetWebFrame()->GetDocument(), nullptr),
      /*to_save=*/false);
  if (!sign_in) {
    return;  // the form went away meanwhile
  }
  if (username && !sign_in->user.IsNull()) {
    sign_in->user.SetAutofillValue(blink::WebString::FromUtf16(*username));
  }
  sign_in->password.SetAutofillValue(blink::WebString::FromUtf16(*password));
  filled_password_ = *password;
}

void FormsAgent::FillDetails(
    const std::optional<base::flat_map<std::string, std::u16string>>& values) {
  if (!values) {
    return;
  }
  for (auto& [token, control] : FindDetails(
           Controls(render_frame()->GetWebFrame()->GetDocument(), nullptr))) {
    auto value = values->find(token);
    // What the user typed already stays.
    if (value != values->end() && control.Value().IsEmpty()) {
      control.SetAutofillValue(blink::WebString::FromUtf16(value->second));
    }
  }
}

}  // namespace lrb
