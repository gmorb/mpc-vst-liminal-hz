// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// http_abi_check.cpp -- engine/http.cpp declares libcurl's option numbers itself (it dlopens libcurl and has no
// header on the device). This compiles only if every one equals the real curl/curl.h value.
#include <curl/curl.h>
static_assert(CURLOPT_WRITEDATA == 10001 && CURLOPT_URL == 10002 && CURLOPT_POSTFIELDS == 10015, "curl opts");
static_assert(CURLOPT_USERAGENT == 10018 && CURLOPT_HTTPHEADER == 10023 && CURLOPT_XFERINFODATA == 10057, "curl opts");
static_assert(CURLOPT_WRITEFUNCTION == 20011 && CURLOPT_XFERINFOFUNCTION == 20219 && CURLOPT_TIMEOUT == 13, "curl opts");
static_assert(CURLOPT_NOPROGRESS == 43 && CURLOPT_FOLLOWLOCATION == 52 && CURLOPT_MAXREDIRS == 68, "curl opts");
static_assert(CURLOPT_CONNECTTIMEOUT == 78 && CURLOPT_NOSIGNAL == 99 && CURLOPT_MAXFILESIZE_LARGE == 30117, "curl opts");
static_assert(CURLINFO_RESPONSE_CODE == 0x200002 && CURL_GLOBAL_DEFAULT == 3, "curl info");
int main() { return 0; }
