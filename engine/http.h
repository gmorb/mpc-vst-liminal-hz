// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* http.h -- HTTPS with the device's own libcurl, loaded at run time (dlopen "libcurl.so.4") and only when a download
 * starts: the plugin never links against it, so it loads and plays on a system without it; only downloads say
 * "needs libcurl". Blocking calls: use them on a worker thread, never the audio thread. `cancel` (optional) is
 * checked during transfers and aborts them (closing the plugin mid-download). */
#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace irrev {

struct HttpResult {
  long status = 0;          // HTTP status; 0 when the request didn't complete
  std::string body;         // response body (not for downloads to a file)
  std::string error;        // transport error text, empty if the request completed
};

class Http {
 public:
  static bool available(std::string* why = nullptr);  // libcurl found and usable
  // headers like "Authorization: Bearer ...". post_form: application/x-www-form-urlencoded body, or nullptr for GET
  static HttpResult request(const std::string& url, const std::vector<std::string>& headers, const std::string* post_form,
                            const std::atomic<bool>* cancel = nullptr, long timeout_s = 30);
  // GET into a file (written to path + ".part", renamed when complete); fails above max_bytes
  static HttpResult download(const std::string& url, const std::vector<std::string>& headers, const std::string& path,
                             size_t max_bytes, const std::atomic<bool>* cancel = nullptr);
  static std::string urlencode(const std::string& s);
};

}  // namespace irrev
