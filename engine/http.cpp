// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "http.h"
#include <dlfcn.h>
#include <cstdio>
#include <mutex>

namespace irrev {

// libcurl's stable ABI (curl/curl.h; checked against the real header by tools/http_abi_check.cpp)
enum : int {
  OPT_WRITEDATA = 10001, OPT_URL = 10002, OPT_POSTFIELDS = 10015, OPT_USERAGENT = 10018, OPT_HTTPHEADER = 10023,
  OPT_XFERINFODATA = 10057, OPT_WRITEFUNCTION = 20011, OPT_XFERINFOFUNCTION = 20219, OPT_TIMEOUT = 13,
  OPT_NOPROGRESS = 43, OPT_FOLLOWLOCATION = 52, OPT_MAXREDIRS = 68, OPT_CONNECTTIMEOUT = 78, OPT_NOSIGNAL = 99,
  OPT_MAXFILESIZE_LARGE = 30117,
};
enum : int { INFO_RESPONSE_CODE = 0x200002 };
enum : long { GLOBAL_DEFAULT = 3 };

typedef void CURL;
struct curl_slist;
static struct Curl {
  bool tried = false, ok = false;
  std::string why;
  int (*global_init)(long);
  CURL* (*easy_init)();
  int (*easy_setopt)(CURL*, int, ...);
  int (*easy_perform)(CURL*);
  int (*easy_getinfo)(CURL*, int, ...);
  void (*easy_cleanup)(CURL*);
  const char* (*easy_strerror)(int);
  curl_slist* (*slist_append)(curl_slist*, const char*);
  void (*slist_free_all)(curl_slist*);
} C;
static std::mutex load_mu;

static bool load() {
  std::lock_guard<std::mutex> lk(load_mu);
  if (C.tried) return C.ok;
  C.tried = true;
  void* h = nullptr;
  for (const char* name : {"libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4"})
    if ((h = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
  if (!h) { C.why = "needs libcurl"; return false; }
#define SYM(field, name) C.field = reinterpret_cast<decltype(C.field)>(dlsym(h, name)); if (!C.field) { C.why = "libcurl too old"; return false; }
  SYM(global_init, "curl_global_init") SYM(easy_init, "curl_easy_init") SYM(easy_setopt, "curl_easy_setopt")
  SYM(easy_perform, "curl_easy_perform") SYM(easy_getinfo, "curl_easy_getinfo") SYM(easy_cleanup, "curl_easy_cleanup")
  SYM(easy_strerror, "curl_easy_strerror") SYM(slist_append, "curl_slist_append") SYM(slist_free_all, "curl_slist_free_all")
#undef SYM
  if (C.global_init(GLOBAL_DEFAULT) != 0) { C.why = "libcurl init failed"; return false; }
  C.ok = true;
  return true;
}

bool Http::available(std::string* why) {
  bool ok = load();
  if (!ok && why) *why = C.why;
  return ok;
}

struct Sink {
  std::string* body = nullptr;
  FILE* file = nullptr;
  size_t bytes = 0, max = 0;
  bool too_big = false;
};
static size_t on_write(char* p, size_t sz, size_t n, void* ud) {
  Sink* s = (Sink*)ud;
  size_t len = sz * n;
  s->bytes += len;
  if (s->max && s->bytes > s->max) { s->too_big = true; return 0; }
  if (s->body) s->body->append(p, len);
  if (s->file && fwrite(p, 1, len, s->file) != len) return 0;
  return len;
}
static int on_progress(void* ud, long long, long long, long long, long long) {
  const std::atomic<bool>* cancel = (const std::atomic<bool>*)ud;
  return (cancel && cancel->load()) ? 1 : 0;       // non-zero aborts the transfer
}

static HttpResult perform(const std::string& url, const std::vector<std::string>& headers, const std::string* post,
                          Sink& sink, const std::atomic<bool>* cancel, long timeout_s) {
  HttpResult r;
  if (!load()) { r.error = C.why; return r; }
  CURL* c = C.easy_init();
  if (!c) { r.error = "libcurl init failed"; return r; }
  curl_slist* hl = nullptr;
  for (auto& hd : headers) hl = C.slist_append(hl, hd.c_str());
  C.easy_setopt(c, OPT_URL, url.c_str());
  C.easy_setopt(c, OPT_NOSIGNAL, 1L);
  C.easy_setopt(c, OPT_FOLLOWLOCATION, 1L);      // curl drops a custom Authorization header on a host change
  C.easy_setopt(c, OPT_MAXREDIRS, 5L);
  C.easy_setopt(c, OPT_CONNECTTIMEOUT, 15L);
  C.easy_setopt(c, OPT_TIMEOUT, timeout_s);
  C.easy_setopt(c, OPT_USERAGENT, "NAM-A2-Lite-for-MPC-OS/1");
  if (hl) C.easy_setopt(c, OPT_HTTPHEADER, hl);
  if (post) C.easy_setopt(c, OPT_POSTFIELDS, post->c_str());
  C.easy_setopt(c, OPT_WRITEFUNCTION, &on_write);
  C.easy_setopt(c, OPT_WRITEDATA, &sink);
  C.easy_setopt(c, OPT_NOPROGRESS, 0L);
  C.easy_setopt(c, OPT_XFERINFOFUNCTION, &on_progress);
  C.easy_setopt(c, OPT_XFERINFODATA, (void*)cancel);
  if (sink.max) C.easy_setopt(c, OPT_MAXFILESIZE_LARGE, (long long)sink.max);
  int rc = C.easy_perform(c);
  if (rc == 0) C.easy_getinfo(c, INFO_RESPONSE_CODE, &r.status);
  else r.error = sink.too_big ? "file too large" : (cancel && cancel->load()) ? "cancelled" : C.easy_strerror(rc);
  if (hl) C.slist_free_all(hl);
  C.easy_cleanup(c);
  return r;
}

HttpResult Http::request(const std::string& url, const std::vector<std::string>& headers, const std::string* post_form,
                         const std::atomic<bool>* cancel, long timeout_s) {
  std::string body;
  Sink s;
  s.body = &body;
  s.max = 8u << 20;                               // API answers are small; refuse anything absurd
  std::vector<std::string> h = headers;
  if (post_form) h.push_back("Content-Type: application/x-www-form-urlencoded");
  HttpResult r = perform(url, h, post_form, s, cancel, timeout_s);
  r.body = std::move(body);
  return r;
}

HttpResult Http::download(const std::string& url, const std::vector<std::string>& headers, const std::string& path,
                          size_t max_bytes, const std::atomic<bool>* cancel) {
  HttpResult r;
  const std::string part = path + ".part";
  FILE* f = fopen(part.c_str(), "wb");
  if (!f) { r.error = "can't write " + path; return r; }
  Sink s;
  s.file = f;
  s.max = max_bytes;
  r = perform(url, headers, nullptr, s, cancel, 300);
  bool ok = fclose(f) == 0 && r.error.empty() && r.status == 200;
  if (ok && rename(part.c_str(), path.c_str()) == 0) return r;
  remove(part.c_str());
  if (r.error.empty()) r.error = r.status ? "HTTP " + std::to_string(r.status) : "download failed";
  return r;
}

std::string Http::urlencode(const std::string& s) {
  static const char* hexd = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
      o += (char)c;
    else { o += '%'; o += hexd[c >> 4]; o += hexd[c & 15]; }
  }
  return o;
}

}  // namespace irrev
