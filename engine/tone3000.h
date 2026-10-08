// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* tone3000.h -- get NAM captures and cab IRs from TONE3000 onto the device (mpc-vst-nam-a2).
 *
 * TONE3000's Select flow (https://www.tone3000.com/api#select), started from a phone on the same Wi-Fi:
 *   1. start(): a small web server on the device's LAN address (port 8191, or the next free one) serves a page with TONE3000's
 *      partnership message and two buttons, amp captures (A2) and cab IRs;
 *   2. the button sends the phone to TONE3000's authorize page (OAuth 2.0 + PKCE, prompt=select_tone), where the
 *      user signs in and browses and picks a tone on TONE3000 itself (search, filters, previews);
 *   3. TONE3000 sends the phone back to this server with a one-time code and the tone's id; this client exchanges
 *      the code for an access token, reads the tone and its models, and downloads the files into the plugin's
 *      own models/<tone>/ or irs/<tone>/ folder, with tone3000.json beside them (title, creator, licence, link).
 * Nothing is stored between picks (no tokens on the device). All of it runs on this object's own threads; the
 * audio thread never sees any of it. HTTPS is the device's own libcurl (libcurl.so.4), loaded when first used:
 * without it the plugin works as before and the status says so.
 * Free, open-source integration: TONE3000's free tier (Select flow only; https://www.tone3000.com/api#commercial-terms).
 */
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace irrev {

// A page the plugin adds to this web server (the My Presets page): handle(method, path, args) fills the reply and
// returns true, or returns false for "not mine". args: the query string and, for a POST, the form fields. Called
// on the server's thread.
struct WebReply {
  std::string status = "200 OK", type = "text/html; charset=utf-8", body, extra;   // extra: more header lines
};
using WebRoute = std::function<bool(const std::string& method, const std::string& path,
                                    const std::map<std::string, std::string>& args, WebReply* out)>;

class Tone3000 {
 public:
  // on_done(path of the first file downloaded, tone title): called from this object's thread after a download
  explicit Tone3000(std::function<void(const std::string&, const std::string&)> on_done);
  ~Tone3000();
  Tone3000(const Tone3000&) = delete;
  Tone3000& operator=(const Tone3000&) = delete;

  // open the page (again); returns at once. kind: "model" or "ir" (the block's Browse TONE3000 button: the page's
  // Continue goes straight to that catalogue), or "" (both choices)
  // need_https=false: only the plugin's own pages are wanted (no TONE3000 sign-in), so a device without libcurl is fine
  void start(const std::string& kind = "", bool need_https = true);
  void set_route(WebRoute r) { route_ = std::move(r); }   // before the first start()
  std::string address() const;    // "192.168.1.20:8191" while the page is open, else ""
  void stop();                    // close it
  std::string status() const;     // one short line for the plugin's page (<= 23 characters where possible)

  // configuration, overridable by environment for tests: MPCNAM_T3K_API (API base URL), MPCNAM_T3K_PORT,
  // MPCNAM_T3K_HOST (the address the phone uses), MPCNAM_T3K_KEY (publishable key), MPCNAM_T3K_DEST (folder
  // holding models/ and irs/; default: the plugin's own folder)
  static std::string publishable_key();

 private:
  void serve();                   // the web server thread
  void handle(int fd);
  void fetch(std::string code, std::string tone_id, std::string kind);   // the download thread
  void set_status(const std::string& s);
  std::string phone_line() const;

  std::function<void(const std::string&, const std::string&)> on_done_;
  WebRoute route_;
  mutable std::mutex mu_;
  std::string status_;
  std::string host_, api_, dest_;
  int port_ = 8191;                // the port in use (set by start() before the server thread)
  int base_port_ = 8191;           // the first one tried
  static constexpr int kPorts = 8; // base_port_ .. base_port_ + 7
  int idle_seconds_ = 900;
  int listen_fd_ = -1;
  std::atomic<bool> serving_{false};
  std::atomic<bool> stop_{false};
  std::atomic<bool> busy_{false};  // a download is running
  std::thread server_, worker_;
  // one pending sign-in at a time (guarded by mu_)
  std::string state_, verifier_, kind_;
  std::string preferred_;          // the kind the page opened for (guarded by mu_)
  long long last_activity_ = 0;
};

}  // namespace irrev
