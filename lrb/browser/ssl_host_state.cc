// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lrb/browser/ssl_host_state.h"

#include "base/functional/callback.h"
#include "net/cert/x509_certificate.h"
#include "url/gurl.h"

namespace lrb {

LrbSslHostState::LrbSslHostState() = default;
LrbSslHostState::~LrbSslHostState() = default;

void LrbSslHostState::AllowCert(const std::string& host,
                                const net::X509Certificate& cert,
                                net::Error error,
                                content::StoragePartition* storage_partition) {
  allowed_[host].insert({cert.CalculateChainFingerprint256(), error});
}

void LrbSslHostState::Clear(
    base::RepeatingCallback<bool(const std::string&)> host_filter) {
  if (host_filter.is_null()) {
    allowed_.clear();
    insecure_content_.clear();
    http_allowed_.clear();
    https_enforced_.clear();
    return;
  }
  std::erase_if(allowed_, [&](const auto& entry) {
    return host_filter.Run(entry.first);
  });
  std::erase_if(insecure_content_, [&](const auto& entry) {
    return host_filter.Run(entry.first);
  });
  std::erase_if(http_allowed_,
                [&](const std::string& host) { return host_filter.Run(host); });
  std::erase_if(https_enforced_,
                [&](const std::string& host) { return host_filter.Run(host); });
}

content::SSLHostStateDelegate::CertJudgment LrbSslHostState::QueryPolicy(
    const std::string& host,
    const net::X509Certificate& cert,
    net::Error error,
    content::StoragePartition* storage_partition) {
  auto it = allowed_.find(host);
  return it != allowed_.end() &&
                 it->second.contains({cert.CalculateChainFingerprint256(),
                                      error})
             ? ALLOWED
             : DENIED;
}

void LrbSslHostState::HostRanInsecureContent(
    const std::string& host,
    InsecureContentType content_type) {
  insecure_content_.insert({host, content_type});
}

bool LrbSslHostState::DidHostRunInsecureContent(
    const std::string& host,
    InsecureContentType content_type) {
  return insecure_content_.contains({host, content_type});
}

void LrbSslHostState::AllowHttpForHost(
    const std::string& host,
    content::StoragePartition* storage_partition) {
  http_allowed_.insert(host);
}

bool LrbSslHostState::IsHttpAllowedForHost(
    const std::string& host,
    content::StoragePartition* storage_partition) {
  return http_allowed_.contains(host);
}

void LrbSslHostState::RevokeUserAllowExceptions(const std::string& host) {
  allowed_.erase(host);
  http_allowed_.erase(host);
}

void LrbSslHostState::SetHttpsEnforcementForHost(
    const std::string& host,
    bool enforce,
    content::StoragePartition* storage_partition) {
  if (enforce) {
    https_enforced_.insert(host);
  } else {
    https_enforced_.erase(host);
  }
}

bool LrbSslHostState::IsHttpsEnforcedForUrl(
    const GURL& url,
    content::StoragePartition* storage_partition) {
  return https_enforced_.contains(url.host());
}

bool LrbSslHostState::HasAllowException(
    const std::string& host,
    content::StoragePartition* storage_partition) {
  return allowed_.contains(host) || http_allowed_.contains(host);
}

bool LrbSslHostState::HasAllowExceptionForAnyHost(
    content::StoragePartition* storage_partition) {
  return !allowed_.empty() || !http_allowed_.empty();
}

}  // namespace lrb
