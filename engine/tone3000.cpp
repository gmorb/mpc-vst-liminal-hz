// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "tone3000.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include "http.h"
#include "library.h"
#include "pkce.h"
#include "web_theme.h"
#include "json.hpp"   // nlohmann/json, from NeuralAmpModelerCore (MIT)

namespace irrev {

// The app's TONE3000 publishable key (a public client id: TONE3000 documents it as safe to embed in client code).
// A build can set another one with -DMPCNAM_T3K_KEY=\"t3k_pub_...\".
#ifndef MPCNAM_T3K_KEY
#define MPCNAM_T3K_KEY "t3k_pub_cQ7aniapKwcVBG-zP37dD_i04iqZcb7Q"
#endif
static const char* kApi = "https://www.tone3000.com/api/v1";
static const long kMaxFile = 64L << 20;           // no single model or IR is anywhere near this
static const int kIdleSeconds = 15 * 60;          // the page closes after this long without use
static const int kMaxFiles = 40;                  // files per pick

static long long now_s() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? v : fallback;
}

// HTTPS is engine/http (the device's own libcurl, loaded when first used; tools/http_abi_check.cpp) and PKCE is
// engine/pkce (SHA-256, base64url, /dev/urandom; tools/pkce_test.cpp checks them against the published vectors).

// ---- small helpers ----------------------------------------------------------------------------------------------
static std::string urlenc(const std::string& s) {
  std::string o;
  char b[4];
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}
static std::string urldec(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else o += s[i];
  }
  return o;
}
static std::map<std::string, std::string> pairs_of(const std::string& s) {   // "a=1&b=2" (a query or a form body)
  std::map<std::string, std::string> q;
  size_t p = 0;
  while (p <= s.size()) {
    size_t amp = s.find('&', p);
    std::string kv = s.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    size_t eq = kv.find('=');
    if (!kv.empty()) q[urldec(kv.substr(0, eq))] = eq == std::string::npos ? "" : urldec(kv.substr(eq + 1));
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  return q;
}
static std::map<std::string, std::string> query_of(const std::string& target) {
  size_t qm = target.find('?');
  return qm == std::string::npos ? std::map<std::string, std::string>() : pairs_of(target.substr(qm + 1));
}
static std::string html_escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}
static std::string safe_name(const std::string& s) {  // a file or folder name from a title
  std::string o;
  for (unsigned char c : s) o += (c < 32 || strchr("/\\:*?\"<>|", c)) ? '-' : (char)c;
  while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
  while (!o.empty() && (o[0] == ' ' || o[0] == '.')) o.erase(0, 1);
  if (o.size() > 80) o.resize(80);
  return o.empty() ? std::string("tone") : o;
}
static std::string lan_address() {                  // the device's IPv4 address on the local network
  std::string best;
  struct ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) != 0) return "";
  for (struct ifaddrs* i = ifs; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
    char b[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in*)i->ifa_addr)->sin_addr, b, sizeof b);
    if (best.empty() || strncmp(i->ifa_name, "wlan", 4) == 0) best = b;   // Wi-Fi first
  }
  freeifaddrs(ifs);
  return best;
}

// ---- the object -------------------------------------------------------------------------------------------------
std::string Tone3000::publishable_key() { return env_or("MPCNAM_T3K_KEY", MPCNAM_T3K_KEY); }

Tone3000::Tone3000(std::function<void(const std::string&, const std::string&)> on_done) : on_done_(std::move(on_done)) {
  api_ = env_or("MPCNAM_T3K_API", kApi);
  // the phone page's first port: NAM A2 8191, Liminal Hz 8191 (so both plugins, or two instances, each
  // get their own page); taken -> the next of kPorts
  base_port_ = atoi(env_or("MPCNAM_T3K_PORT", "8191").c_str());
  port_ = base_port_;
  idle_seconds_ = atoi(env_or("MPCNAM_T3K_IDLE", std::to_string(kIdleSeconds)).c_str());   // tests: shorter
  dest_ = env_or("MPCNAM_T3K_DEST", Library::plugin_dir());
}

Tone3000::~Tone3000() { stop(); }

// "Open 10.0.0.5:8191", or just the address when "Open " + it is over 21 characters (the status box beside BROWSE is
// about 17 characters of address wide at the page's text size)
std::string Tone3000::phone_line() const {
  const std::string addr = host_ + ":" + std::to_string(port_);
  return addr.size() + 5 <= 21 ? "Open " + addr : addr;
}

void Tone3000::set_status(const std::string& s) {
  std::lock_guard<std::mutex> lk(mu_);
  status_ = s;
}

std::string Tone3000::status() const {
  std::lock_guard<std::mutex> lk(mu_);
  return status_;
}

std::string Tone3000::address() const {
  std::lock_guard<std::mutex> lk(mu_);
  return serving_ ? host_ + ":" + std::to_string(port_) : std::string();
}

void Tone3000::start(const std::string& kind, bool need_https) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    preferred_ = "space";                         // Liminal Hz: one catalogue (reverb and space IRs)
  }
  if (serving_) {                                   // open: just show the address again
    std::lock_guard<std::mutex> lk(mu_);
    last_activity_ = now_s();
    if (!busy_ && need_https) status_ = phone_line();
    return;
  }
  if (server_.joinable()) server_.join();           // the page closed itself (idle): open it again
  std::string why;
  if (need_https && !Http::available(&why)) { set_status(why == "needs libcurl" ? "Needs libcurl" : why); return; }
  host_ = env_or("MPCNAM_T3K_HOST", lan_address());
  if (host_.empty()) { set_status("No network"); return; }
  // the port, here (before the server thread): the first free one of base_port_ .. base_port_ + kPorts - 1
  int s = -1;
  for (int p = base_port_; p < base_port_ + kPorts && s < 0; p++) {
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { set_status("Can't open the page"); return; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   // (not SO_REUSEPORT: a taken port stays taken)
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)p);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr*)&a, sizeof a) == 0 && listen(s, 4) == 0) { port_ = p; break; }
    close(s);
    s = -1;
  }
  if (s < 0) {
    set_status("Ports " + std::to_string(base_port_) + "-" + std::to_string(base_port_ + kPorts - 1) + " in use");
    return;
  }
  listen_fd_ = s;
  stop_ = false;
  last_activity_ = now_s();
  serving_ = true;
  if (need_https) set_status(phone_line());
  server_ = std::thread(&Tone3000::serve, this);
}

void Tone3000::stop() {
  stop_ = true;                                     // also aborts a transfer in progress (Http's cancel flag)
  if (server_.joinable()) server_.join();
  if (worker_.joinable()) worker_.join();
}

void Tone3000::serve() {                            // start() has bound listen_fd_
  const int s = listen_fd_;
  while (!stop_) {
    struct pollfd p = {s, POLLIN, 0};
    if (poll(&p, 1, 250) > 0) {
      int c = accept(s, nullptr, nullptr);
      if (c >= 0) { handle(c); close(c); }
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (!busy_ && now_s() - last_activity_ > idle_seconds_) { status_ = "Page closed"; break; }
  }
  close(s);
  listen_fd_ = -1;
  serving_ = false;                                 // start() opens it again
}

static void reply(int fd, const std::string& status, const std::string& type, const std::string& body,
                  const std::string& extra = "") {
  std::string r = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " +
                  std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n" + extra + "\r\n" + body;
  size_t off = 0;
  while (off < r.size()) {
    ssize_t n = send(fd, r.data() + off, r.size() - off, MSG_NOSIGNAL);
    if (n <= 0) break;
    off += (size_t)n;
  }
}

// <plugin folder>/art/tone3000-logo(.svg|.png): put TONE3000's official logo there (from tone3000.com/api,
// "Download TONE3000 Logos") and the phone page shows it; without it, a text wordmark
static std::string logo_base() { return Library::plugin_dir() + "/art/tone3000-logo"; }

static std::string page(const std::string& body, bool mark = true) {   // the Liminal Hz look (web_theme.h)
  return web_page("Liminal Hz", (mark ? web_wordmark() : std::string()) + body);
}

void Tone3000::handle(int fd) {
  struct pollfd p = {fd, POLLIN, 0};
  if (poll(&p, 1, 3000) <= 0) return;
  char buf[8192];
  ssize_t n = recv(fd, buf, sizeof buf - 1, 0);
  if (n <= 0) return;
  std::string req(buf, (size_t)n), method, target;
  size_t sp1 = req.find(' '), sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) { reply(fd, "400 Bad Request", "text/plain", "bad request"); return; }
  method = req.substr(0, sp1);
  target = req.substr(sp1 + 1, sp2 - sp1 - 1);
  const std::string path = target.substr(0, target.find('?'));
  auto q = query_of(target);
  if (method == "POST") {                           // a form (the presets page): read the body, up to 4 KB
    const size_t hdr_end = req.find("\r\n\r\n");
    if (hdr_end == std::string::npos) { reply(fd, "400 Bad Request", "text/plain", "bad request"); return; }
    std::string head = req.substr(0, hdr_end);
    for (char& ch : head) ch = (char)tolower((unsigned char)ch);
    size_t cl = head.find("content-length:");
    const long want = cl == std::string::npos ? 0 : atol(head.c_str() + cl + 15);
    if (want < 0 || want > 4096) { reply(fd, "413 Payload Too Large", "text/plain", "too large"); return; }
    std::string body = req.substr(hdr_end + 4);
    while ((long)body.size() < want) {
      struct pollfd pb = {fd, POLLIN, 0};
      if (poll(&pb, 1, 3000) <= 0) break;
      ssize_t m = recv(fd, buf, sizeof buf, 0);
      if (m <= 0) break;
      body.append(buf, (size_t)m);
    }
    if ((long)body.size() > want) body.resize((size_t)want);
    for (auto& kv : pairs_of(body)) q[kv.first] = kv.second;     // (the form's fields win over the query's)
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    last_activity_ = now_s();
  }
  if (method == "GET" && (path == "/font/regular.ttf" || path == "/font/semibold.ttf")) {   // the pages' typeface
    const std::string f = Library::plugin_dir() + "/art/fonts/TitilliumWeb-" + (path[6] == 'r' ? "Regular" : "SemiBold") + ".ttf";
    FILE* fp = fopen(f.c_str(), "rb");
    if (!fp) { reply(fd, "404 Not Found", "text/plain", "no font"); return; }   // (the pages fall back to the system font)
    std::string data;
    char b2[8192];
    size_t got;
    while ((got = fread(b2, 1, sizeof b2, fp)) > 0 && data.size() < (1u << 20)) data.append(b2, got);
    fclose(fp);
    reply(fd, "200 OK", "font/ttf", data, "Cache-Control: max-age=86400\r\n");
    return;
  }
  if (route_) {
    WebReply r;
    if (route_(method, path, q, &r)) { reply(fd, r.status, r.type, r.body, r.extra); return; }
  }
  if (method != "GET") { reply(fd, "405 Method Not Allowed", "text/plain", "GET only"); return; }
  const std::string self = "http://" + host_ + ":" + std::to_string(port_);

  if (path == "/logo") {                            // the official TONE3000 logo, if the plugin folder has it
    for (const char* ext : {".svg", ".png"}) {
      FILE* f = fopen((logo_base() + ext).c_str(), "rb");
      if (!f) continue;
      std::string img;
      char b2[4096];
      size_t got;
      while ((got = fread(b2, 1, sizeof b2, f)) > 0 && img.size() < (1u << 20)) img.append(b2, got);
      fclose(f);
      reply(fd, "200 OK", ext[1] == 's' ? "image/svg+xml" : "image/png", img);
      return;
    }
    reply(fd, "404 Not Found", "text/plain", "no logo");
    return;
  }
  if (path == "/") {                                // TONE3000's splash introducing the integration (design requirement 2; its copy is a recommendation, not fixed)
    std::string pref;
    {
      std::lock_guard<std::mutex> lk(mu_);
      pref = preferred_;
    }
    const bool have_logo = access((logo_base() + ".svg").c_str(), R_OK) == 0 ||
                           access((logo_base() + ".png").c_str(), R_OK) == 0;
    const std::string logo = have_logo ? "<img class=logoimg src='/logo' alt='TONE3000'>" : "<span class=logo>TONE3000</span>";
    (void)pref;
    std::string why;
    const bool can_t3k = Http::available(&why);
    const std::string browse = can_t3k
        ? "<a class='b go' href='/go?kind=space'>Continue: space, outboard, pedal and experimental IRs</a>"
        : "<p><small>TONE3000 can't be reached from this MPC (" + why + "). Your presets still can, below.</small></p>";
    reply(fd, "200 OK", "text/html; charset=utf-8", page(
        web_wordmark("<span class=x>&times;</span>" + logo) +
        "<div class=cap>tone3000</div><div class=card>"
        "<p>Liminal Hz can load impulse responses (IRs) from TONE3000, a massive library of captures of real "
        "spaces and gear, created by a global community of musicians.</p>" + browse +
        "<p><small>You sign in and pick a tone on TONE3000; it downloads straight to your MPC, into the plugin's "
        "reverbs folder. Keep this phone on the same Wi-Fi as the MPC.</small></p></div>"
        "<div class=cap>my presets</div><div class=card>"
        "<p>Save the plugin's current sound under a name you type, and load, rename or delete your presets.</p>"
        "<a class=b href='/presets'>My Presets: name &amp; save</a></div>", false));
    return;
  }
  if (path == "/go") {                              // start TONE3000's Select flow (OAuth 2.0 + PKCE)
    const std::string kind = "space";             // Liminal Hz: reverb and space IRs only
    std::string url;
    {
      std::lock_guard<std::mutex> lk(mu_);
      kind_ = kind;
      state_ = random_token(24);                    // 32 URL-safe characters
      verifier_ = random_token(48);                 // 64 characters (RFC 7636: 43 to 128)
      url = api_ + "/oauth/authorize?client_id=" + urlenc(publishable_key()) + "&redirect_uri=" + urlenc(self + "/cb") +
            "&response_type=code&code_challenge=" + pkce_challenge(verifier_) + "&code_challenge_method=S256&state=" +
            state_ + "&prompt=select_tone&menubar=true&preview=true" +
            std::string("&format=ir&gears=space_outboard_pedal_experimental");   // spaces, outboard (echo, plate...), pedals and experimental: IRs only (format=ir)
      status_ = "Signing in on phone";
    }
    reply(fd, "302 Found", "text/plain", "", "Location: " + url + "\r\n");
    return;
  }
  if (path == "/cb") {                              // back from TONE3000
    std::string kind, code = q["code"], tone = q["tone_id"];
    bool ok;
    {
      std::lock_guard<std::mutex> lk(mu_);
      ok = !state_.empty() && q["state"] == state_;
      kind = kind_;
      if (ok) state_.clear();                       // one use
    }
    if (!ok) { reply(fd, "400 Bad Request", "text/html; charset=utf-8", page("<h1>That link has expired</h1><p><a class=b href='/'>Start again</a></p>")); return; }
    if (!tone.empty() && tone.find_first_not_of("0123456789") != std::string::npos) tone.clear();   // ids are numbers
    if (q["canceled"] == "true" || tone.empty() || code.empty()) {
      set_status("Canceled");
      reply(fd, "200 OK", "text/html; charset=utf-8", page("<h1>Nothing picked</h1><p><a class=b href='/'>Browse again</a></p>"));
      return;
    }
    if (busy_) { reply(fd, "200 OK", "text/html; charset=utf-8", page("<h1>Still downloading the last pick</h1><p><a class=b href='/'>Back</a></p>")); return; }
    busy_ = true;
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread(&Tone3000::fetch, this, code, tone, kind);
    reply(fd, "200 OK", "text/html; charset=utf-8", page(
        "<div class=logo>TONE3000</div><h1>Downloading to your MPC</h1><p>Watch the plugin's TONE3000 line: it "
        "selects the new IR when it's done.</p>"
        "<a class=b href='/'>Pick another</a>"));
    return;
  }
  reply(fd, "404 Not Found", "text/plain", "not found");
}

void Tone3000::fetch(std::string code, std::string tone_id, std::string kind) {
  std::string verifier;
  {
    std::lock_guard<std::mutex> lk(mu_);
    verifier = verifier_;
  }
  const std::string self = "http://" + host_ + ":" + std::to_string(port_);
  auto fail = [&](const std::string& msg) { set_status(msg); busy_ = false; };
  set_status("Signing in...");
  const std::string form = "grant_type=authorization_code&code=" + urlenc(code) + "&code_verifier=" + urlenc(verifier) +
                           "&redirect_uri=" + urlenc(self + "/cb") + "&client_id=" + urlenc(publishable_key());
  HttpResult r = Http::request(api_ + "/oauth/token", {}, &form, &stop_);
  if (r.status != 200) return fail(!r.error.empty() ? r.error : "Sign-in failed (" + std::to_string(r.status) + ")");
  std::string token;
  try { token = nlohmann::json::parse(r.body).value("access_token", ""); } catch (...) {}
  if (token.empty()) return fail("Sign-in failed");

  const std::string arch = "";                     // IRs: no NAM architecture
  const std::vector<std::string> auth = {"Authorization: Bearer " + token};
  r = Http::request(api_ + "/tones/" + tone_id + "", auth, nullptr, &stop_);
  if (r.status != 200) return fail(!r.error.empty() ? r.error : "TONE3000 error " + std::to_string(r.status));
  nlohmann::json tone;
  try { tone = nlohmann::json::parse(r.body); } catch (...) { return fail("Bad reply from TONE3000"); }
  { const std::string fmt = tone.value("format", ""); if (!fmt.empty() && fmt != "ir") return fail("Not an IR"); }   // IRs only
  const std::string title = tone.value("title", std::string("tone ") + tone_id);
  r = Http::request(api_ + "/models?tone_id=" + tone_id + "&page_size=300" + arch, auth, nullptr, &stop_);
  if (r.status != 200) return fail(!r.error.empty() ? r.error : "TONE3000 error " + std::to_string(r.status));
  nlohmann::json models;
  try { models = nlohmann::json::parse(r.body)["data"]; } catch (...) { return fail("Bad reply from TONE3000"); }
  if (!models.is_array() || models.empty()) return fail("No files in that tone");

  // <models|irs>/TONE3000/<tone title>/: the folder names the source, so the plugin's info line credits TONE3000
  const std::string base = dest_ + "/reverbs";       // the plugin's reverbs/ folder
  const std::string dir = base + "/TONE3000/" + safe_name(title);
  mkdir(base.c_str(), 0755);
  mkdir((base + "/TONE3000").c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::string first;
  int done = 0, total = (int)models.size();
  for (auto& m : models) {
    if (done >= kMaxFiles) break;                   // a sane bound for one pick
    std::string url = m.value("model_url", ""), name = m.value("name", "");
    if (url.empty()) { total--; continue; }
    std::string ext = ".wav";
    size_t dot = url.find_last_of('.'), slash = url.find_last_of('/');
    if (dot != std::string::npos && dot > slash) ext = url.substr(dot);
    if (ext != ".wav") { total--; continue; }       // only what this plugin loads: IRs (never a NAM model)
    set_status("Downloading " + std::to_string(done + 1) + "/" + std::to_string(total));
    std::string file = dir + "/" + safe_name(name.empty() ? std::to_string(m.value("id", 0)) : name);
    std::string target = file + ext;
    for (int k = 2; access(target.c_str(), F_OK) == 0; k++) target = file + " (" + std::to_string(k) + ")" + ext;
    r = Http::download(url, auth, target, kMaxFile, &stop_);
    if (!r.error.empty()) return fail(r.error == "cancelled" ? "Canceled" : "Download failed");
    if (first.empty()) first = target;
    done++;
  }
  if (first.empty()) return fail("No usable files");
  // attribution for the files (TONE3000's design requirements: creator and origin travel with the tone)
  nlohmann::json meta = {{"source", "TONE3000"}, {"tone_id", tone.value("id", 0)}, {"title", title},
                         {"creator", tone.contains("user") ? tone["user"].value("username", "") : ""},
                         {"license", tone.value("license", "")}, {"url", tone.value("url", "")},
                         {"gear", tone.value("gear", "")}, {"format", tone.value("format", "")}};
  if (FILE* f = fopen((dir + "/tone3000.json").c_str(), "w")) { fputs(meta.dump(1).c_str(), f); fclose(f); }
  {                                                       // "Got <title> (n)" in 23 characters: the count stays
    const std::string count = done > 1 ? " (" + std::to_string(done) + ")" : "";
    std::string t = title;
    const size_t room = 23 - 4 - count.size();
    if (t.size() > room) t = t.substr(0, room - 3) + "...";
    set_status("Got " + t + count);
  }
  busy_ = false;
  if (on_done_) on_done_(first, title);
}

}  // namespace irrev
