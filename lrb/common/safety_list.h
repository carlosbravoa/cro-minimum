// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_COMMON_SAFETY_LIST_H_
#define LRB_COMMON_SAFETY_LIST_H_

#include "base/files/file_path.h"

class GURL;

namespace lrb {

// Phishing and malware warnings (decided 2026-10-14, in place of Google
// Safe Browsing): public lists of phishing pages and malware downloads
// (malware-filter's phishing and URLhaus lists, uBlock Origin's badware),
// compiled into an engine file of their own, apart from the ad lists, so
// turning blocking off doesn't turn these off. The coordinator fetches them
// when lrb starts, never while it runs. Shared by every instance like the
// ad engine: ~0.85 MB of each instance's memory.
class SafetyList {
 public:
  // Loads the engine file (ListUpdater, setting "safety"). Blocking. Call
  // once, at startup; until then, and if it fails, nothing is listed.
  static bool Load(const base::FilePath& path);
  static bool loaded();

  // Whether the lists name `url` (a page, or a download) as dangerous.
  static bool Lists(const GURL& url);
};

}  // namespace lrb

#endif  // LRB_COMMON_SAFETY_LIST_H_
