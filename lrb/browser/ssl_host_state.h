// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_BROWSER_SSL_HOST_STATE_H_
#define LRB_BROWSER_SSL_HOST_STATE_H_

#include <map>
#include <set>
#include <string>
#include <utility>

#include "content/public/browser/ssl_host_state_delegate.h"
#include "net/base/hash_value.h"

namespace lrb {

// Certificates the user let through ("Continue anyway",
// certificate_errors.h), for this site's process only: kept in memory, so
// asked again after a restart, as Chrome does after a while. content asks
// it whether the page's other requests to that host may pass; without it
// (content_shell's) they all failed. Also what content records about
// insecure content and HTTPS-First, which lrb doesn't use.
class LrbSslHostState : public content::SSLHostStateDelegate {
 public:
  LrbSslHostState();
  LrbSslHostState(const LrbSslHostState&) = delete;
  LrbSslHostState& operator=(const LrbSslHostState&) = delete;
  ~LrbSslHostState() override;

  // content::SSLHostStateDelegate:
  void AllowCert(const std::string& host,
                 const net::X509Certificate& cert,
                 net::Error error,
                 content::StoragePartition* storage_partition) override;
  void Clear(
      base::RepeatingCallback<bool(const std::string&)> host_filter) override;
  CertJudgment QueryPolicy(
      const std::string& host,
      const net::X509Certificate& cert,
      net::Error error,
      content::StoragePartition* storage_partition) override;
  void HostRanInsecureContent(const std::string& host,
                              InsecureContentType content_type) override;
  bool DidHostRunInsecureContent(const std::string& host,
                                 InsecureContentType content_type) override;
  void AllowHttpForHost(const std::string& host,
                        content::StoragePartition* storage_partition) override;
  bool IsHttpAllowedForHost(
      const std::string& host,
      content::StoragePartition* storage_partition) override;
  void RevokeUserAllowExceptions(const std::string& host) override;
  void SetHttpsEnforcementForHost(
      const std::string& host,
      bool enforce,
      content::StoragePartition* storage_partition) override;
  bool IsHttpsEnforcedForUrl(
      const GURL& url,
      content::StoragePartition* storage_partition) override;
  bool HasAllowException(const std::string& host,
                         content::StoragePartition* storage_partition) override;
  bool HasAllowExceptionForAnyHost(
      content::StoragePartition* storage_partition) override;

 private:
  // Host -> the certificate chains (fingerprints) and errors let through.
  std::map<std::string, std::set<std::pair<net::SHA256HashValue, net::Error>>>
      allowed_;
  std::set<std::pair<std::string, InsecureContentType>> insecure_content_;
  std::set<std::string, std::less<>> http_allowed_;
  std::set<std::string, std::less<>> https_enforced_;
};

}  // namespace lrb

#endif  // LRB_BROWSER_SSL_HOST_STATE_H_
