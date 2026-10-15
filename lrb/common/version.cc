// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/common/version.h"

namespace lrb {

std::string_view Version() {
  return LRB_VERSION;
}

std::string_view SourceCommit() {
  return LRB_SOURCE;
}

std::string_view ChromiumReleased() {
  return LRB_CHROMIUM_RELEASED;
}

}  // namespace lrb
