// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* web_theme.h -- the phone pages' look, shared by the TONE3000 pages (tone3000.cpp) and My Presets (presets_web.cpp).
 *
 * The plugin page's palette and type: near-black with a faint warm glow, frames drawn as thin borders, ivory text,
 * lowercase mono captions ("trim", "fade"...), Titillium Web (served from the plugin folder's art/fonts, the system
 * font if it's missing), the LIMINAL Hz wordmark. Amber marks what's live (the selected preset, a notice), as on the
 * MPC. No scripts, no outside requests: the pages work on a phone with no internet.
 */
#pragma once
#include <string>

namespace irrev {

inline const char* web_css() {
  return
      "@font-face{font-family:LHz;src:url(/font/regular.ttf) format('truetype');font-weight:400}"
      "@font-face{font-family:LHz;src:url(/font/semibold.ttf) format('truetype');font-weight:600 800}"
      ":root{--bg:#0a0b0d;--panel:#101114;--line:#2a2a2e;--ink:#e0ddd6;--dim:#9a958c;--faint:#5e5a54;--amber:#d9b77a}"
      "*{box-sizing:border-box}"
      "body{margin:0;min-height:100vh;color:var(--ink);font-family:LHz,system-ui,sans-serif;line-height:1.45;"
      "background:radial-gradient(ellipse 80% 45% at 85% 105%,rgba(217,183,122,.10),transparent 70%),"
      "radial-gradient(ellipse 60% 40% at 0% 0%,rgba(140,150,170,.06),transparent 70%),var(--bg)}"
      ".wrap{max-width:34em;margin:auto;padding:1.4em 1.2em 2em}"
      ".wm{display:flex;align-items:baseline;gap:.45em;margin:.3em 0 1.2em;flex-wrap:wrap}"
      ".wm1{font-weight:400;letter-spacing:.32em;font-size:1.45em}.wm2{font-weight:700;font-size:1.45em}"
      ".by{color:var(--dim);font-size:.8em}.x{color:var(--faint);margin:0 .1em}.logoimg{height:1.3em;align-self:center}"
      ".logo{font-weight:800;letter-spacing:.06em;font-size:1.2em}"
      ".cap{font-family:ui-monospace,Menlo,monospace;text-transform:lowercase;color:var(--dim);font-size:.85em;"
      "letter-spacing:.04em;margin:1.2em 0 .45em}"
      ".card{background:rgba(16,17,20,.88);border:1px solid var(--line);border-radius:4px;padding:1em;margin:.5em 0 1em}"
      "h1{font-size:1.25em;font-weight:600;margin:.2em 0 .5em}p{margin:.5em 0}small,.sub{color:var(--dim)}"
      ".msg{border:1px solid var(--line);border-left:3px solid var(--amber);background:rgba(217,183,122,.07);"
      "padding:.6em .8em;margin:.8em 0;border-radius:4px}"
      "a.b,button{display:inline-block;font:inherit;font-weight:600;font-size:.82em;letter-spacing:.14em;"
      "text-transform:uppercase;text-decoration:none;text-align:center;color:var(--ink);background:#1d1f23;"
      "border:1px solid var(--line);border-radius:4px;padding:.75em 1em;margin:.3em .3em 0 0;cursor:pointer}"
      "a.b{display:block;margin:.6em 0;padding:1em}"
      "a.b.go,button.go{background:var(--ink);color:#0a0b0d;border-color:var(--ink)}"
      "button.del{color:#e6a49a;border-color:#4a2a28}"
      "input[type=text]{width:100%;font:inherit;font-size:1.05em;padding:.65em .7em;border-radius:4px;"
      "border:1px solid var(--line);background:#07080a;color:var(--ink);margin:.35em 0}"
      "input[type=text]:focus{outline:none;border-color:var(--dim)}"
      "label{font-size:.9em;color:var(--dim)}.row{border-top:1px solid var(--line);padding:.8em 0}"
      ".row:first-child{border-top:0;padding-top:.1em}.name{font-weight:600;font-size:1.1em}"
      ".cur{color:var(--amber);font-size:.8em;margin-left:.5em;letter-spacing:.1em;text-transform:uppercase}"
      "form{display:inline}.ren{display:flex;gap:.4em;margin-top:.4em;align-items:center}.ren input{flex:1;margin:0}"
      ".ren button{margin:0}.nav{margin-top:1.6em}";
}

inline std::string web_page(const std::string& title, const std::string& body) {
  return "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<meta name=theme-color content='#0a0b0d'><title>" + title + "</title><style>" + web_css() + "</style></head>"
         "<body><div class=wrap>" + body + "</div></body></html>";
}

// the wordmark, as on the plugin page (extra: what follows it, e.g. " x TONE3000")
inline std::string web_wordmark(const std::string& extra = "") {
  return "<div class=wm><span class=wm1>LIMINAL</span><span class=wm2>Hz</span>" + extra + "</div>";
}

}  // namespace irrev
