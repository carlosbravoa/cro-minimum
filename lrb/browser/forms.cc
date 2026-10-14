// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/browser/forms.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/document_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "lrb/browser/keyring.h"
#include "lrb/browser/shell.h"
#include "lrb/browser/ui/lrb_platform_delegate.h"
#include "lrb/browser/ui/window_view.h"
#include "url/origin.h"

namespace lrb {

namespace {

using Question = WindowView::Question;

// Which kind of detail an autocomplete token is, in the questions' words;
// empty for one lrb doesn't keep (cards, passwords, one-time codes).
std::u16string Kind(const std::string& token) {
  static constexpr const char* kNames[] = {
      "name",        "given-name",       "additional-name", "family-name",
      "nickname",    "honorific-prefix", "honorific-suffix"};
  if (std::ranges::find(kNames, token) != std::end(kNames)) {
    return u"name";
  }
  if (token == "email") {
    return u"email";
  }
  if (token == "tel" || token.starts_with("tel-")) {
    return u"phone";
  }
  if (token == "organization") {
    return u"company";
  }
  if (token == "street-address" || token.starts_with("address-") ||
      token == "postal-code" || token == "country" ||
      token == "country-name") {
    return u"address";
  }
  return std::u16string();
}

// "name, email and address", in a fixed order.
std::u16string Describe(const std::vector<std::string>& tokens) {
  std::set<std::u16string> kinds;
  for (const std::string& token : tokens) {
    if (std::u16string kind = Kind(token); !kind.empty()) {
      kinds.insert(std::move(kind));
    }
  }
  std::vector<std::u16string> words;
  for (const char16_t* kind :
       {u"name", u"email", u"phone", u"address", u"company"}) {
    if (kinds.contains(kind)) {
      words.push_back(kind);
    }
  }
  std::u16string text;
  for (size_t i = 0; i < words.size(); ++i) {
    if (i > 0) {
      text += i + 1 == words.size() ? u" and " : u", ";
    }
    text += words[i];
  }
  return text;
}

class FormsHost : public content::DocumentService<mojom::Forms> {
 public:
  FormsHost(content::RenderFrameHost& frame,
            mojo::PendingReceiver<mojom::Forms> receiver)
      : DocumentService(frame, std::move(receiver)) {}
  FormsHost(const FormsHost&) = delete;
  FormsHost& operator=(const FormsHost&) = delete;
  ~FormsHost() override = default;

  // mojom::Forms:
  void PasswordSubmitted(const std::u16string& username,
                         const std::u16string& password) override {
    const std::string origin = Origin();
    if (origin.empty() || !PasswordFits(username, password) || !View()) {
      return;
    }
    ListPasswords(origin, base::BindOnce(&FormsHost::OnSubmittedUsers,
                                         weak_factory_.GetWeakPtr(), username,
                                         password));
  }

  void OfferPassword(OfferPasswordCallback callback) override {
    const std::string origin = Origin();
    if (origin.empty() || !View()) {
      std::move(callback).Run(std::nullopt, std::nullopt);
      return;
    }
    ListPasswords(origin,
                  base::BindOnce(&FormsHost::OnUsersToFill,
                                 weak_factory_.GetWeakPtr(), std::move(callback)));
  }

  void DetailsSubmitted(
      const base::flat_map<std::string, std::u16string>& fields) override {
    Details details;
    for (const auto& [token, value] : fields) {
      if (!Kind(token).empty() && !value.empty()) {
        details[token] = value;
      }
    }
    // Two at least: one field alone is a search box or a newsletter.
    if (details.size() < 2 || !DetailsFit(details) || Origin().empty() ||
        !View()) {
      return;
    }
    // Is there a keyring at all? (Nothing saved looks like no keyring.)
    ListPasswords(Origin(),
                  base::BindOnce(&FormsHost::OnKeyringForDetails,
                                 weak_factory_.GetWeakPtr(), std::move(details)));
  }

  void OfferDetails(const std::vector<std::string>& fields,
                    OfferDetailsCallback callback) override {
    if (Origin().empty() || !View()) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    GetDetails(base::BindOnce(&FormsHost::OnDetailsToFill,
                              weak_factory_.GetWeakPtr(), fields,
                              std::move(callback)));
  }

 private:
  // "https://host[:port]" of the page; empty if it isn't an http(s) one.
  std::string Origin() const {
    const url::Origin& page = origin();
    if (page.opaque() || (page.scheme() != "https" && page.scheme() != "http")) {
      return std::string();
    }
    return page.Serialize();
  }

  std::u16string Host() const { return base::UTF8ToUTF16(origin().host()); }

  // The page's window, if its tab is the one on screen.
  WindowView* View() const {
    content::WebContents* contents =
        content::WebContents::FromRenderFrameHost(&render_frame_host());
    Shell* shell = contents ? Shell::FromWebContents(contents) : nullptr;
    WindowView* view = shell ? LrbPlatformDelegate::ViewFor(shell) : nullptr;
    return view && view->shell() == shell ? view : nullptr;
  }

  // Asked without taking focus: these come with the page, not a click.
  void Ask(Question question) {
    if (WindowView* view = View()) {
      question.owner = this;
      question.focus = Question::Focus::kNone;
      view->Ask(std::move(question));
    }
  }

  void OnSubmittedUsers(
      const std::u16string& username,
      const std::u16string& password,
      std::optional<std::vector<std::u16string>> users) {
    if (!users) {
      return;  // no keyring
    }
    if (std::ranges::find(*users, username) == users->end()) {
      AskToSave(username, password, /*update=*/false);
      return;
    }
    GetPassword(Origin(), username,
                base::BindOnce(
                    [](base::WeakPtr<FormsHost> self, std::u16string username,
                       std::u16string password,
                       std::optional<std::u16string> saved) {
                      if (self && saved != password) {
                        self->AskToSave(username, password, /*update=*/true);
                      }
                    },
                    weak_factory_.GetWeakPtr(), username, password));
  }

  void AskToSave(const std::u16string& username,
                 const std::u16string& password,
                 bool update) {
    Question question;
    question.text =
        update ? base::StrCat({u"Update the saved password for ", username,
                               u" on ", Host(), u"?"})
               : base::StrCat({u"Save the password for ", username, u" on ",
                               Host(), u" in your desktop's keyring?"});
    question.accept_label = update ? u"Update" : u"Save";
    question.cancel_label = u"Not now";
    question.answer = base::BindOnce(
        [](std::string origin, std::u16string username,
           std::u16string password, std::optional<bool> accepted,
           const std::u16string&) {
          if (accepted.value_or(false)) {
            SavePassword(origin, username, password, base::DoNothing());
          }
        },
        Origin(), username, password);
    Ask(std::move(question));
  }

  void OnUsersToFill(OfferPasswordCallback callback,
                     std::optional<std::vector<std::u16string>> users) {
    if (!users || users->empty() || !View()) {
      std::move(callback).Run(std::nullopt, std::nullopt);
      return;
    }
    Question question;
    question.accept_label = u"Fill in";
    question.cancel_label = u"Not now";
    const bool several = users->size() > 1;
    if (several) {
      // Which account: typed (or kept) in the row's text field.
      question.text = base::StrCat({u"Fill in a saved password? Accounts: ",
                                    base::JoinString(*users, u", ")});
      question.input = true;
      question.default_input = users->front();
    } else {
      question.text = base::StrCat(
          {u"Fill in the saved password for ", users->front(), u"?"});
    }
    question.answer = base::BindOnce(
        [](base::WeakPtr<FormsHost> self, OfferPasswordCallback callback,
           std::u16string only, bool several, std::optional<bool> accepted,
           const std::u16string& input) {
          if (!self || !accepted.value_or(false)) {
            std::move(callback).Run(std::nullopt, std::nullopt);
            return;
          }
          const std::u16string username = several ? input : only;
          GetPassword(
              self->Origin(), username,
              base::BindOnce(
                  [](OfferPasswordCallback callback, std::u16string username,
                     std::optional<std::u16string> password) {
                    if (password) {
                      std::move(callback).Run(username, *password);
                    } else {
                      std::move(callback).Run(std::nullopt, std::nullopt);
                    }
                  },
                  std::move(callback), username));
        },
        weak_factory_.GetWeakPtr(), std::move(callback), users->front(),
        several);
    Ask(std::move(question));
  }

  void OnKeyringForDetails(Details details,
                           std::optional<std::vector<std::u16string>> users) {
    if (!users) {
      return;  // no keyring
    }
    GetDetails(base::BindOnce(&FormsHost::OnSavedDetails,
                              weak_factory_.GetWeakPtr(), std::move(details)));
  }

  void OnSavedDetails(Details submitted, Details saved) {
    Details merged = saved;
    std::vector<std::string> changed;
    for (const auto& [token, value] : submitted) {
      if (saved[token] != value) {
        changed.push_back(token);
      }
      merged[token] = value;
    }
    if (changed.empty() || !DetailsFit(merged)) {
      return;  // known already, or too much to keep
    }
    Question question;
    question.text = base::StrCat({u"Remember your ", Describe(changed),
                                  u" to fill in forms on every site?"});
    question.accept_label = u"Remember";
    question.cancel_label = u"Not now";
    question.answer = base::BindOnce(
        [](Details merged, std::optional<bool> accepted,
           const std::u16string&) {
          if (accepted.value_or(false)) {
            SaveDetails(merged, base::DoNothing());
          }
        },
        std::move(merged));
    Ask(std::move(question));
  }

  void OnDetailsToFill(std::vector<std::string> fields,
                       OfferDetailsCallback callback,
                       Details saved) {
    base::flat_map<std::string, std::u16string> values;
    std::vector<std::string> tokens;
    for (const std::string& token : fields) {
      auto it = saved.find(token);
      if (it != saved.end() && !Kind(token).empty()) {
        values[token] = it->second;
        tokens.push_back(token);
      }
    }
    if (values.empty() || !View()) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    Question question;
    question.text =
        base::StrCat({u"Fill in your saved ", Describe(tokens), u"?"});
    question.accept_label = u"Fill in";
    question.cancel_label = u"Not now";
    question.answer = base::BindOnce(
        [](OfferDetailsCallback callback,
           base::flat_map<std::string, std::u16string> values,
           std::optional<bool> accepted, const std::u16string&) {
          std::move(callback).Run(
              accepted.value_or(false) ? std::optional(std::move(values))
                                       : std::nullopt);
        },
        std::move(callback), std::move(values));
    Ask(std::move(question));
  }

  base::WeakPtrFactory<FormsHost> weak_factory_{this};
};

}  // namespace

void BindForms(content::RenderFrameHost* frame,
               mojo::PendingReceiver<mojom::Forms> receiver) {
  // The page's own frame only: sign-ins inside frames (ads, embeds) aren't
  // offered.
  if (!frame || frame->GetParent() || !frame->IsInPrimaryMainFrame()) {
    return;
  }
  new FormsHost(*frame, std::move(receiver));  // deleted with the document
}

}  // namespace lrb
