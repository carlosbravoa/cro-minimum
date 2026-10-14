// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/common/safety_list.h"

#include <atomic>
#include <string>

#include "base/logging.h"
#include "lrb/adblock/lib.rs.h"
#include "lrb/common/content_blocker.h"
#include "url/gurl.h"

namespace lrb {

namespace {

// Set once at startup, then only read; never freed.
std::atomic<const adblock::Engine*> g_engine{nullptr};

}  // namespace

// static
bool SafetyList::Load(const base::FilePath& path) {
  const adblock::Engine* engine =
      LoadEngineFile(path, /*with_resources=*/false);
  if (!engine) {
    LOG(WARNING) << "lrb: no phishing and malware warnings";
    return false;
  }
  g_engine.store(engine, std::memory_order_release);
  VLOG(1) << "lrb: phishing and malware lists from " << path;
  return true;
}

// static
bool SafetyList::loaded() {
  return g_engine.load(std::memory_order_acquire) != nullptr;
}

// static
bool SafetyList::Lists(const GURL& url) {
  const adblock::Engine* engine = g_engine.load(std::memory_order_acquire);
  if (!engine || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  // As a page opened by itself: the lists' rules are for pages ($all,
  // $document) and apply whoever links there.
  const std::string& spec = url.spec();
  return engine
      ->check(rust::Str(spec.data(), spec.size()),
              rust::Str(spec.data(), spec.size()), rust::Str("document"),
              rust::Str("GET"))
      .block;
}

}  // namespace lrb
