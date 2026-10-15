// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/browser/safety_throttle.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "lrb/browser/shell.h"
#include "lrb/browser/ui/lrb_platform_delegate.h"
#include "lrb/browser/ui/window_view.h"
#include "lrb/common/safety_list.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

namespace lrb {

namespace {

// Hosts the user chose to continue to, until this instance exits.
std::set<std::string, std::less<>>& Continued() {
  static base::NoDestructor<std::set<std::string, std::less<>>> hosts;
  return *hosts;
}

}  // namespace

SafetyThrottle::SafetyThrottle(content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

SafetyThrottle::~SafetyThrottle() {
  // Unanswered (the user went elsewhere, the window closed): the question
  // goes too. Not back here: this throttle is going.
  weak_factory_.InvalidateWeakPtrs();
  if (asked_in_) {
    Shell* shell = Shell::FromWebContents(asked_in_.get());
    if (WindowView* view = shell ? LrbPlatformDelegate::ViewFor(shell)
                                 : nullptr) {
      view->DropQuestionsFrom(this);
    }
  }
}

content::NavigationThrottle::ThrottleCheckResult
SafetyThrottle::WillStartRequest() {
  return Check();
}

content::NavigationThrottle::ThrottleCheckResult
SafetyThrottle::WillRedirectRequest() {
  return Check();
}

const char* SafetyThrottle::GetNameForLogging() {
  return "lrb::SafetyThrottle";
}

content::NavigationThrottle::ThrottleCheckResult SafetyThrottle::Check() {
  content::NavigationHandle* handle = navigation_handle();
  const GURL& url = handle->GetURL();
  if (!handle->IsInPrimaryMainFrame() || !SafetyList::Lists(url) ||
      Continued().contains(url.host())) {
    return PROCEED;
  }
  content::WebContents* contents = handle->GetWebContents();
  Shell* shell = Shell::FromWebContents(contents);
  WindowView* view = shell ? LrbPlatformDelegate::ShowTabFor(shell) : nullptr;
  if (!view) {
    return ThrottleCheckResult(BLOCK_REQUEST, net::ERR_BLOCKED_BY_CLIENT);
  }
  WindowView::Question question;
  question.text = base::StrCat(
      {base::UTF8ToUTF16(url.host()),
       u" is on lists of phishing and malware sites: it may try to steal "
       u"your passwords or install harmful software."});
  question.accept_label = u"Continue anyway";
  question.cancel_label = u"Back";
  question.focus = WindowView::Question::Focus::kCancel;
  question.owner = this;
  question.answer = base::BindOnce(&SafetyThrottle::OnAnswer,
                                   weak_factory_.GetWeakPtr(),
                                   std::string(url.host()));
  asked_in_ = contents->GetWeakPtr();
  view->Ask(std::move(question));
  return DEFER;
}

void SafetyThrottle::OnAnswer(std::string host,
                              std::optional<bool> accepted,
                              const std::u16string& input) {
  asked_in_.reset();
  if (accepted.value_or(false)) {
    Continued().insert(std::move(host));
    Resume();
    return;
  }
  // Back: the page before stays. A window with none would be left blank:
  // it shows that the page was blocked instead.
  content::NavigationEntry* shown = navigation_handle()
                                        ->GetWebContents()
                                        ->GetController()
                                        .GetLastCommittedEntry();
  CancelDeferredNavigation(
      shown && !shown->IsInitialEntry()
          ? ThrottleCheckResult(CANCEL)
          : ThrottleCheckResult(BLOCK_REQUEST, net::ERR_BLOCKED_BY_CLIENT));
}

}  // namespace lrb
