// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// pkce_test.cpp -- engine/pkce against published test vectors: NIST FIPS 180-4 SHA-256 examples, RFC 4648
// base64 examples (in their URL-safe, unpadded form) and RFC 7636 Appendix B's PKCE S256 example.
#include <cstdio>
#include <string>
#include "pkce.h"
using namespace irrev;
static int fails = 0;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static std::string hex(const std::string& s) { std::string o; char b[3]; for (unsigned char c : s) { snprintf(b, 3, "%02x", c); o += b; } return o; }
int main() {
  CHECK(hex(sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of \"\"");
  CHECK(hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of \"abc\" (FIPS 180-4)");
  CHECK(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 of the 448-bit message (two blocks)");
  CHECK(hex(sha256(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of one million 'a'");
  CHECK(base64url("f") == "Zg" && base64url("fo") == "Zm8" && base64url("foo") == "Zm9v" && base64url("foobar") == "Zm9vYmFy",
        "base64url, RFC 4648 examples without padding");
  CHECK(base64url(std::string("\xfb\xff", 2)) == "-_8", "base64url uses - and _");
  CHECK(pkce_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
        "PKCE S256 challenge, RFC 7636 Appendix B");
  std::string a = random_token(32), b = random_token(32);
  CHECK(a.size() == 43 && b.size() == 43 && a != b, "random 32-byte verifiers: 43 URL-safe characters, different each time");
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
