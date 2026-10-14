// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LRB_BROWSER_KEYRING_H_
#define LRB_BROWSER_KEYRING_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"

namespace lrb {

// Saved passwords and autofill details, asked of the coordinator, which
// keeps them in the desktop's keyring (lrb/coordinator/keyring_broker.h).
// Nothing is kept in this process. Without a coordinator, or without a
// keyring, there are none.
//
// `origin`: "https://host[:port]" of this instance's site (the coordinator
// refuses others). Values are capped so a request fits the coordinator's
// line: user names at 256 bytes, passwords at 1024, details at 2400.

// The user names saved for `origin`; nothing if there's no keyring (then
// lrb never offers to save).
void ListPasswords(
    const std::string& origin,
    base::OnceCallback<void(std::optional<std::vector<std::u16string>>)> done);
void GetPassword(
    const std::string& origin,
    const std::u16string& username,
    base::OnceCallback<void(std::optional<std::u16string>)> done);
void SavePassword(const std::string& origin,
                  const std::u16string& username,
                  const std::u16string& password,
                  base::OnceCallback<void(bool)> done);

// Autofill details, one set for every site, by autocomplete token
// ("email", "postal-code").
using Details = std::map<std::string, std::u16string>;
void GetDetails(base::OnceCallback<void(Details)> done);
void SaveDetails(const Details& details, base::OnceCallback<void(bool)> done);

// Whether these fit a request (else they aren't offered for saving).
bool PasswordFits(const std::u16string& username,
                  const std::u16string& password);
bool DetailsFit(const Details& details);

// A line from the coordinator: true if it was an answer to one of these.
bool OnKeyringLine(const std::string& line);

}  // namespace lrb

#endif  // LRB_BROWSER_KEYRING_H_
