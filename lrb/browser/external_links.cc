// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/browser/external_links.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/web_contents.h"
#include "lrb/browser/lrb_content_browser_client.h"
#include "lrb/browser/shell.h"
#include "lrb/browser/ui/lrb_platform_delegate.h"
#include "lrb/browser/ui/window_view.h"
#include "lrb/coordinator/rules.h"
#include "url/gurl.h"

namespace lrb {

namespace {

// Longer links are cut short in the question.
constexpr size_t kShownLength = 80;

void Open(const GURL& url) {
  LrbContentBrowserClient* client = LrbContentBrowserClient::Get();
  if (client && client->has_coordinator()) {
    client->SendToCoordinator("open-app " +
                              base::EscapeAllExceptUnreserved(url.spec()));
    return;
  }
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](std::string link) {
            base::Process process =
                base::LaunchProcess({"xdg-open", link}, base::LaunchOptions());
            if (process.IsValid()) {
              base::EnsureProcessGetsReaped(std::move(process));
            }
          },
          url.spec()));
}

}  // namespace

void AskToOpenInApp(content::WebContents* contents,
                    const GURL& url,
                    bool clicked,
                    bool from_page) {
  if (!coordinator::IsExternalAppUrl(url.spec())) {
    VLOG(1) << "lrb: not a link for an app: " << url.scheme() << ":";
    return;
  }
  if (!clicked && !from_page) {
    VLOG(1) << "lrb: a frame inside the page opened a " << url.scheme()
            << ": link without a click";
    return;
  }
  Shell* shell = Shell::FromWebContents(contents);
  WindowView* view = shell ? LrbPlatformDelegate::ViewFor(shell) : nullptr;
  if (!view || view->shell() != shell || view->HasQuestionsFrom(contents)) {
    return;  // a tab not on screen, or this page already asked
  }
  std::u16string link = base::UTF8ToUTF16(url.spec());
  if (link.size() > kShownLength) {
    link = base::StrCat({link.substr(0, kShownLength - 1), u"…"});
  }
  WindowView::Question question;
  question.text = base::StrCat({u"Open ", link, u" in another app?"});
  question.accept_label = u"Open";
  question.cancel_label = u"Cancel";
  // After a click the row may take focus, on Cancel: a stray Enter never
  // opens anything.
  question.focus = clicked ? WindowView::Question::Focus::kCancel
                           : WindowView::Question::Focus::kNone;
  question.owner = contents;
  question.answer = base::BindOnce(
      [](GURL url, std::optional<bool> accepted, const std::u16string&) {
        if (accepted.value_or(false)) {
          Open(url);
        }
      },
      url);
  view->Ask(std::move(question));
}

}  // namespace lrb
