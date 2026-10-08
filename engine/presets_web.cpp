// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "presets_web.h"
#include <cstdio>
#include "presets.h"
#include "web_theme.h"

namespace irrev {

static std::string esc(const std::string& s) {       // for HTML text and attribute values
  std::string o;
  for (char c : s) {
    if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}
static std::string urlenc(const std::string& s) {
  std::string o;
  char b[4];
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}
static std::string arg(const std::map<std::string, std::string>& a, const char* k) {
  auto it = a.find(k);
  return it == a.end() ? std::string() : it->second;
}

static std::string page(const std::string& body) {   // the Liminal Hz look (web_theme.h)
  return web_page("Liminal Hz presets", web_wordmark() + body);
}

static void back(WebReply* out, const std::string& msg) {            // 303: reloading the page doesn't repeat the POST
  out->status = "303 See Other";
  out->type = "text/plain";
  out->body = "";
  out->extra = "Location: /presets" + (msg.empty() ? std::string() : "?msg=" + urlenc(msg)) + "\r\n";
}

bool presets_web_route(const PresetsWebHooks& h, const std::string& method, const std::string& path,
                       const std::map<std::string, std::string>& args, WebReply* out) {
  if (path.compare(0, 8, "/presets") != 0) return false;
  if (path == "/presets" || path == "/presets/") {
    if (method != "GET") { out->status = "405 Method Not Allowed"; out->type = "text/plain"; out->body = "GET only"; return true; }
    const std::string cur = h.current ? h.current() : std::string();
    std::string b = "<div class=cap>my presets</div><div class=sub>Name, load, rename and delete the presets you saved. "
                    "They are files in the plugin's presets folder.</div>";
    const std::string msg = arg(args, "msg");
    if (!msg.empty()) b += "<div class=msg>" + esc(msg) + "</div>";
    b += "<div class=card><form method=post action='/presets/save'>"
         "<label for=n>Save the plugin's current sound as</label>"
         "<input type=text id=n name=name maxlength=40 placeholder='Preset name' autocomplete=off>"
         "<label><input type=checkbox name=overwrite value=1> replace it if the name exists</label><br>"
         "<button class=go type=submit>Save current sound</button></form></div>";
    auto list = Presets::list();
    b += "<div class=card>";
    if (list.empty()) b += "<small>No saved presets yet. Set the plugin the way you like it, then save it above (or tap SAVE in the MY PRESETS list).</small>";
    for (const auto& p : list) {
      const std::string n = esc(p.name);
      b += "<div class=row><span class=name>" + n + "</span>" + (p.name == cur ? "<span class=cur>selected</span>" : "") + "<br>"
           "<form method=post action='/presets/load'><input type=hidden name=name value=\"" + n + "\"><button type=submit>Load</button></form>"
           "<form method=post action='/presets/delete' onsubmit=\"return confirm('Delete this preset?')\">"
           "<input type=hidden name=name value=\"" + n + "\"><button class=del type=submit>Delete</button></form>"
           "<form method=post action='/presets/rename' class=ren><input type=hidden name=from value=\"" + n + "\">"
           "<input type=text name=to value=\"" + n + "\" maxlength=40 autocomplete=off><button type=submit>Rename</button></form></div>";
    }
    b += "</div><p><small>Keep this phone on the same Wi-Fi as the MPC. The page closes itself after 15 idle minutes.</small></p>"
         "<div class=nav><a class=b href='/'>Home: TONE3000 IRs</a></div>";
    out->body = page(b);
    return true;
  }
  if (method != "POST") { out->status = "405 Method Not Allowed"; out->type = "text/plain"; out->body = "POST only"; return true; }
  std::string err;
  if (path == "/presets/save") {
    const std::string name = Presets::clean_name(arg(args, "name"));
    if (name.empty()) { back(out, "Type a name first."); return true; }
    err = h.save ? h.save(name, arg(args, "overwrite") == "1") : "Not available";
    back(out, err.empty() ? "Saved \"" + name + "\"." : err);
  } else if (path == "/presets/load") {
    err = h.load ? h.load(arg(args, "name")) : "Not available";
    back(out, err.empty() ? "Loaded \"" + arg(args, "name") + "\"." : err);
  } else if (path == "/presets/delete") {
    err = h.remove ? h.remove(arg(args, "name")) : "Not available";
    back(out, err.empty() ? "Deleted \"" + arg(args, "name") + "\"." : err);
  } else if (path == "/presets/rename") {
    const std::string to = Presets::clean_name(arg(args, "to"));
    err = h.rename ? h.rename(arg(args, "from"), to) : "Not available";
    back(out, err.empty() ? "Renamed to \"" + to + "\"." : err);
  } else {
    out->status = "404 Not Found"; out->type = "text/plain"; out->body = "not found";
  }
  return true;
}

}  // namespace irrev
