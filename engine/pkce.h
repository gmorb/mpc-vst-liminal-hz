// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* pkce.h -- what OAuth 2.0 PKCE (RFC 7636) needs: SHA-256 (FIPS 180-4), base64url without padding, and random
 * URL-safe strings from /dev/urandom. Small and dependency-free (no OpenSSL on the device is assumed). */
#pragma once
#include <cstdint>
#include <string>

namespace irrev {
std::string sha256(const std::string& data);                    // 32 raw bytes
std::string base64url(const std::string& bytes);                // RFC 4648 section 5, no '=' padding
std::string random_token(int bytes);                            // base64url of `bytes` random bytes ("" on failure)
std::string pkce_challenge(const std::string& verifier);        // base64url(sha256(verifier)): method S256
}  // namespace irrev
