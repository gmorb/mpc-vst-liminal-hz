// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* presets_web.h -- the My Presets page of the phone web server (engine/tone3000.h's server; open it from the
 * My Presets list's NAME ON PHONE button, then http://<address>/presets on a phone or computer on the same Wi-Fi).
 *
 * The MPC's screen has no keyboard, so naming happens here: a text box to save the current sound under a name,
 * and for each saved preset Load, Rename and Delete. The page only asks; the plugin does the work through these
 * hooks (each returns "" when it worked, else a short message for the page's banner).
 * Every change is a POST, answered with a redirect back to the page, so a reload never repeats it.
 */
#pragma once
#include <functional>
#include <map>
#include <string>
#include "tone3000.h"

namespace irrev {

struct PresetsWebHooks {
  std::function<std::string(const std::string& name, bool overwrite)> save;   // the plugin's current sound
  std::function<std::string(const std::string& name)> load, remove;
  std::function<std::string(const std::string& from, const std::string& to)> rename;
  std::function<std::string()> current;                                       // the selected preset's name
};

// true when `path` is one of this page's (the caller replies with *out)
bool presets_web_route(const PresetsWebHooks& hooks, const std::string& method, const std::string& path,
                       const std::map<std::string, std::string>& args, WebReply* out);

}  // namespace irrev
