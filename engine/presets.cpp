// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// Plain POSIX file calls with 64-bit offsets, as library.cpp (see the note there).
#if !defined(_FILE_OFFSET_BITS) || _FILE_OFFSET_BITS != 64
#error "build with -D_FILE_OFFSET_BITS=64 (large-file directory listing; see library.cpp)"
#endif
#include "presets.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "library.h"

namespace irrev {

static const char kExt[] = ".lhzp";
static const size_t kExtLen = 5;
static const char kHead[] = "liminal-hz-preset=1";
static const size_t kMaxFile = 16 * 1024;

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string Presets::dir() {
  const char* e = std::getenv("MPCNAM_PRESETS");
  if (e && *e) return e;
  const std::string own = Library::plugin_dir();
  return own.empty() ? std::string() : own + "/presets";
}

static bool has_ext(const std::string& f) {
  return f.size() > kExtLen && lower(f.substr(f.size() - kExtLen)) == kExt;
}

// the preset files in the folder: (name, file name), unsorted
static std::vector<std::pair<std::string, std::string>> files_in(const std::string& dir) {
  std::vector<std::pair<std::string, std::string>> out;
  DIR* d = dir.empty() ? nullptr : opendir(dir.c_str());
  if (!d) return out;
  while (struct dirent* e = readdir(d)) {
    if (e->d_name[0] == '.' || !has_ext(e->d_name)) continue;
    if (e->d_type != DT_REG) {                              // a link or folder: not a preset (UNKNOWN: ask)
      if (e->d_type != DT_UNKNOWN) continue;
      struct stat st;
      if (lstat((dir + "/" + e->d_name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
    }
    const std::string f = e->d_name;
    out.emplace_back(f.substr(0, f.size() - kExtLen), f);
  }
  closedir(d);
  return out;
}

static bool read_small(const std::string& path, std::string* out) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  char buf[2048];
  size_t n;
  out->clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out->size() <= kMaxFile) out->append(buf, n);
  fclose(f);
  return out->size() <= kMaxFile;
}

static bool parse(const std::string& name, const std::string& text, PresetData* p) {
  size_t pos = 0;
  bool head = false;
  PresetData r;
  r.name = name;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(pos, nl - pos);
    pos = nl + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!head) { if (line != kHead) return false; head = true; continue; }
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "ir_src") r.ir_src = atoi(v.c_str()) == 1 ? 1 : 0;
    else if (k == "ir_path") r.ir_path = v;
    else if (k == "ir_display") r.ir_display = v;
    else if (k == "ir_file") r.ir_file = v;
    else {
      const float f = (float)atof(v.c_str());
      if (std::isfinite(f)) r.values.emplace_back(k, f);
    }
  }
  if (!head) return false;
  *p = std::move(r);
  return true;
}

bool Presets::load(const std::string& name, PresetData* out) {
  const std::string d = dir();
  for (const auto& f : files_in(d)) {
    if (lower(f.first) != lower(name)) continue;
    std::string text;
    return read_small(d + "/" + f.second, &text) && parse(f.first, text, out);
  }
  return false;
}

std::vector<PresetData> Presets::list() {
  std::vector<PresetData> out;
  const std::string d = dir();
  for (const auto& f : files_in(d)) {
    std::string text;
    PresetData p;
    if (read_small(d + "/" + f.second, &text) && parse(f.first, text, &p)) out.push_back(std::move(p));
  }
  std::sort(out.begin(), out.end(), [](const PresetData& a, const PresetData& b) {
    const std::string la = lower(a.name), lb = lower(b.name);
    return la != lb ? la < lb : a.name < b.name;
  });
  if ((int)out.size() > kMaxPresets) out.resize(kMaxPresets);
  return out;
}

std::string Presets::clean_name(const std::string& raw) {
  std::string o;
  for (unsigned char c : raw) o += (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) ? '-' : (char)c;
  size_t a = 0, b = o.size();
  while (a < b && (o[a] == ' ' || o[a] == '.')) a++;
  while (b > a && (o[b - 1] == ' ' || o[b - 1] == '.')) b--;
  o = o.substr(a, b - a);
  if (o.size() > kMaxName) {
    size_t cut = kMaxName;
    while (cut > 0 && ((unsigned char)o[cut] & 0xC0) == 0x80) cut--;   // not in the middle of a character
    o.resize(cut);
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
  }
  return o;
}

bool Presets::exists(const std::string& name) {
  const std::string want = lower(name);
  for (const auto& f : files_in(dir())) if (lower(f.first) == want) return true;
  return false;
}

std::string Presets::unique_name(const std::string& base) {
  std::string b = clean_name(base);
  if (b.empty()) b = "My Preset";
  if (!exists(b)) return b;
  for (int k = 2; k < 10000; k++) {
    std::string tail = " " + std::to_string(k);
    std::string head = b;
    while (head.size() + tail.size() > kMaxName && head.size() > 1) head.pop_back();   // (ASCII-safe enough: re-cleaned)
    const std::string cand = clean_name(head + tail);
    if (!cand.empty() && !exists(cand)) return cand;
  }
  return b;
}

static void make_dirs(const std::string& path) {          // like mkdir -p
  for (size_t i = 1; i <= path.size(); i++)
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

static std::string one_line(std::string s) {
  for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
  return s;
}

bool Presets::save(const PresetData& p, bool overwrite, std::string* err) {
  auto fail = [err](const char* why) { if (err) *err = why; return false; };
  const std::string d = dir();
  if (d.empty()) return fail("No presets folder");
  const std::string name = clean_name(p.name);
  if (name.empty()) return fail("Give it a name");
  std::string stem = name;                                  // an existing file of this name (any case) is the target
  bool found = false;
  for (const auto& f : files_in(d)) if (lower(f.first) == lower(name)) { stem = f.first; found = true; break; }
  if (found && !overwrite) return fail("That name is already used");
  make_dirs(d);
  std::string text = std::string(kHead) + "\n";
  char buf[96];
  for (const auto& kv : p.values) {
    if (kv.first.empty() || kv.first.find_first_of("=\n\r") != std::string::npos || !std::isfinite(kv.second)) continue;
    snprintf(buf, sizeof buf, "=%.6g\n", kv.second);
    text += kv.first + buf;
  }
  if (!p.ir_file.empty() || !p.ir_path.empty()) {
    text += "ir_src=" + std::string(p.ir_src == 1 ? "1" : "0") + "\nir_path=" + one_line(p.ir_path) +
            "\nir_display=" + one_line(p.ir_display) + "\nir_file=" + one_line(p.ir_file) + "\n";
  }
  const std::string tmp = d + "/." + stem + ".tmp", path = d + "/" + stem + kExt;
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return fail("Can't write the presets folder");
  const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  const bool closed = fclose(f) == 0;
  if (!ok || !closed || ::rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return fail("Can't save the preset"); }
  return true;
}

bool Presets::remove(const std::string& name, std::string* err) {
  const std::string d = dir();
  for (const auto& f : files_in(d)) {
    if (lower(f.first) != lower(name)) continue;
    if (unlink((d + "/" + f.second).c_str()) == 0) return true;
    if (err) *err = "Can't delete the preset";
    return false;
  }
  if (err) *err = "Preset not found";
  return false;
}

bool Presets::rename(const std::string& from, const std::string& to, std::string* err) {
  auto fail = [err](const char* why) { if (err) *err = why; return false; };
  const std::string d = dir(), name = clean_name(to);
  if (name.empty()) return fail("Give it a name");
  std::string old;
  for (const auto& f : files_in(d)) if (lower(f.first) == lower(from)) { old = f.second; break; }
  if (old.empty()) return fail("Preset not found");
  if (lower(name) != lower(from) && exists(name)) return fail("That name is already used");
  if (::rename((d + "/" + old).c_str(), (d + "/" + name + kExt).c_str()) != 0) return fail("Can't rename the preset");
  return true;
}

}  // namespace irrev
