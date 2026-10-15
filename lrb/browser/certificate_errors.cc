// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/browser/certificate_errors.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "lrb/browser/lrb_content_browser_client.h"
#include "lrb/browser/shell.h"
#include "lrb/browser/ui/lrb_platform_delegate.h"
#include "lrb/browser/ui/window_view.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace lrb {

namespace {

using Result = content::CertificateRequestResultType;
using Answer = base::OnceCallback<void(Result)>;
constexpr Result kContinue = content::CERTIFICATE_REQUEST_RESULT_TYPE_CONTINUE;
constexpr Result kDeny = content::CERTIFICATE_REQUEST_RESULT_TYPE_DENY;

// Answers 204 with no content; a network that intercepts traffic answers
// with its sign-in page (or a redirect to it) instead. Chrome's captive
// portal check uses the same.
constexpr char kProbeUrl[] = "http://www.gstatic.com/generate_204";
constexpr char kProbeSite[] = "gstatic.com";
constexpr base::TimeDelta kProbeTimeout = base::Seconds(3);

constexpr net::NetworkTrafficAnnotationTag kProbeAnnotation =
    net::DefineNetworkTrafficAnnotation("lrb_captive_portal", R"(
      semantics {
        sender: "lrb certificate errors"
        description: "Checks whether the network intercepts traffic until "
          "the user signs in (a hotel or airport Wi-Fi), which shows up as a "
          "certificate error on HTTPS pages."
        trigger: "A page's certificate isn't valid."
        data: "None: a plain GET of a fixed URL, no cookies."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Not configurable."
      })");

// What's wrong, after "<host>'s certificate".
std::u16string Problem(int cert_error) {
  switch (cert_error) {
    case net::ERR_CERT_DATE_INVALID:
      return u"has expired or isn't valid yet (or this computer's clock is "
             u"wrong)";
    case net::ERR_CERT_COMMON_NAME_INVALID:
      return u"is for a different site";
    case net::ERR_CERT_AUTHORITY_INVALID:
      return u"isn't from an authority this computer trusts";
    case net::ERR_CERT_REVOKED:
      return u"was revoked";
    case net::ERR_CERT_WEAK_SIGNATURE_ALGORITHM:
    case net::ERR_CERT_WEAK_KEY:
      return u"uses weak cryptography";
    default:
      return base::StrCat(
          {u"isn't valid (",
           base::ASCIIToUTF16(net::ErrorToShortString(cert_error)), u")"});
  }
}

// Whether the network intercepts traffic: anything but a 204 from the
// probe. A network error or a timeout says nothing: no.
void ProbeForSignIn(content::BrowserContext* context,
                    base::OnceCallback<void(bool)> done) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kProbeUrl);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kProbeAnnotation);
  loader->SetTimeoutDuration(kProbeTimeout);
  network::SimpleURLLoader* raw = loader.get();
  raw->DownloadHeadersOnly(
      context->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> loader,
             base::OnceCallback<void(bool)> done,
             scoped_refptr<net::HttpResponseHeaders> headers) {
            std::move(done).Run(headers && headers->response_code() != 204);
          },
          std::move(loader), std::move(done)));
}

void Ask(int cert_error,
         base::WeakPtr<content::WebContents> contents,
         const GURL& url,
         bool strict,
         Answer answer,
         bool sign_in) {
  Shell* shell = contents ? Shell::FromWebContents(contents.get()) : nullptr;
  WindowView* view = shell ? LrbPlatformDelegate::ShowTabFor(shell) : nullptr;
  if (!view) {
    std::move(answer).Run(kDeny);
    return;
  }
  const std::u16string host = base::UTF8ToUTF16(url.host());
  WindowView::Question question;
  if (sign_in) {
    question.text = base::StrCat(
        {host, u" can't be reached securely: this Wi-Fi network may want "
               u"you to sign in first."});
    question.accept_label = u"Sign in to Wi-Fi";
    question.cancel_label = u"Back";
    question.focus = WindowView::Question::Focus::kAccept;
  } else if (strict) {
    question.text = base::StrCat(
        {host, u"'s certificate ", Problem(cert_error),
         u". The site requires a valid one, so it can't be opened."});
    question.accept_label = u"Back";  // the only way
    question.focus = WindowView::Question::Focus::kAccept;
  } else {
    question.text =
        base::StrCat({host, u"'s certificate ", Problem(cert_error),
                      u": someone may be impersonating the site."});
    question.accept_label = u"Continue anyway";
    question.cancel_label = u"Back";
    question.focus = WindowView::Question::Focus::kCancel;
  }
  question.answer = base::BindOnce(
      [](bool sign_in, bool strict,
         base::WeakPtr<content::WebContents> contents, Answer answer,
         std::optional<bool> accepted, const std::u16string&) {
        const bool yes = accepted.value_or(false);
        if (sign_in && yes) {
          if (LrbContentBrowserClient* client =
                  LrbContentBrowserClient::Get()) {
            client->OpenSiteWindow(kProbeSite, GURL(kProbeUrl));
          }
        }
        if (yes && !sign_in && !strict) {
          std::move(answer).Run(kContinue);
          return;
        }
        std::move(answer).Run(kDeny);
        // Back: to the page before, if there is one; else the error page
        // stays.
        if (accepted.has_value() && contents &&
            contents->GetController().CanGoBack()) {
          contents->GetController().GoBack();
        }
      },
      sign_in, strict, contents, std::move(answer));
  view->Ask(std::move(question));
}

}  // namespace

void AskAboutCertificateError(content::WebContents* contents,
                              int cert_error,
                              const GURL& url,
                              bool strict,
                              Answer answer) {
  ProbeForSignIn(contents->GetBrowserContext(),
                 base::BindOnce(&Ask, cert_error, contents->GetWeakPtr(), url,
                                strict, std::move(answer)));
}

}  // namespace lrb
