// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_COMMON_VERSION_H_
#define LRB_COMMON_VERSION_H_

#include <string_view>

namespace lrb {

// What this build is, for About: set by build/build.sh (lrb/BUILD.gn's
// lrb_* args); empty in a build made without it.

// The release this source is or will be: "0.1.0-alpha.6" (VERSION).
std::string_view Version();

// The source's commit, "7bb1884c12ab", with "+changes" when the build had
// uncommitted changes.
std::string_view SourceCommit();

// The day Chromium released the version lrb is built on to Stable on
// Linux, "2026-10-06" (CHROMIUM_RELEASE). The version itself is
// version_info::GetVersionNumber().
std::string_view ChromiumReleased();

}  // namespace lrb

#endif  // LRB_COMMON_VERSION_H_
