// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_BROWSER_SAFETY_THROTTLE_H_
#define LRB_BROWSER_SAFETY_THROTTLE_H_

#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {
class WebContents;
}  // namespace content

namespace lrb {

// A page (or a download) the phishing and malware lists name
// (SafetyList): before anything is fetched from it, the window says so and
// offers Back, focused, and Continue anyway, as Chrome's warning does. Back
// stays on the page before; in a window with none, an error page says it
// was blocked. A Continue is remembered for the host until the site's
// window closes. The page's own frame only: frames inside it are the ad
// lists' business.
class SafetyThrottle : public content::NavigationThrottle {
 public:
  explicit SafetyThrottle(content::NavigationThrottleRegistry& registry);
  ~SafetyThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult Check();
  void OnAnswer(std::string host,
                std::optional<bool> accepted,
                const std::u16string& input);

  // The window asked in, to take the question back if the navigation goes
  // away unanswered.
  base::WeakPtr<content::WebContents> asked_in_;
  base::WeakPtrFactory<SafetyThrottle> weak_factory_{this};
};

}  // namespace lrb

#endif  // LRB_BROWSER_SAFETY_THROTTLE_H_
