// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
//
// Directory walking uses POSIX opendir/readdir/lstat with 64-bit file offsets (_FILE_OFFSET_BITS=64, set by the
// build), not std::filesystem: a 32-bit program's readdir fails with EOVERFLOW ("value too large") when a
// filesystem hands out directory offsets or inode numbers beyond 32 bits, and the C++ library's std::filesystem is
// compiled without large-file support, so it can't be fixed from here. Measured under qemu-arm on a 64-bit host;
// the same can happen on devices with large filesystems.
#if !defined(_FILE_OFFSET_BITS) || _FILE_OFFSET_BITS != 64
#error "build with -D_FILE_OFFSET_BITS=64 (large-file directory listing; see above)"
#endif
#include "library.h"
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace irrev {

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string Library::plugin_dir() {
  Dl_info info;
  if (dladdr(reinterpret_cast<void*>(&Library::plugin_dir), &info) && info.dli_fname) {
    std::string p = info.dli_fname;
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
  }
  return "";
}

std::string Library::media_root() {
  const char* e = std::getenv("MPCNAM_MEDIA");
  return (e && *e) ? e : "/media";
}

enum Kind { NONE, DIR_, FILE_ };
static Kind kind_of(const std::string& p) {        // lstat: a link is neither (never followed: no loops)
  struct stat st;
  if (lstat(p.c_str(), &st) != 0) return NONE;
  if (S_ISDIR(st.st_mode)) return DIR_;
  if (S_ISREG(st.st_mode)) return FILE_;
  return NONE;
}

// names in a directory (no ".", "..", or hidden ones such as macOS's "._name.wav" and ".Trashes"), sorted
static std::vector<std::string> names_in(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (struct dirent* e = readdir(d))
    if (e->d_name[0] != '.') out.push_back(e->d_name);
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

// every directory named `name` (any case) under root, down to max_depth levels below it
static void find_dirs(const std::string& root, const std::string& name, int depth, int max_depth,
                      std::vector<std::string>& out) {
  if (depth >= max_depth) return;
  for (const std::string& n : names_in(root)) {
    std::string p = root + "/" + n;
    if (kind_of(p) != DIR_) continue;
    if (lower(n) == "deleted") continue;                  // never scanned (earlier versions moved files there)
    if (lower(n) == name) { out.push_back(p); continue; }   // a models/ folder's contents are listed separately
    find_dirs(p, name, depth + 1, max_depth, out);
  }
}

// tone3000.json in a folder (GET TONES writes it beside the files it downloads): title, gear, format, creator.
// A small flat JSON object; read without a JSON library (library.cpp stays dependency-free).
static std::string json_str(const std::string& j, const std::string& key) {
  size_t k = j.find("\"" + key + "\"");
  if (k == std::string::npos) return "";
  size_t c = j.find(':', k), q = c == std::string::npos ? c : j.find('"', c);
  if (q == std::string::npos) return "";
  std::string v;
  for (size_t i = q + 1; i < j.size() && j[i] != '"'; i++) {
    if (j[i] == '\\' && i + 1 < j.size()) { i++; v += j[i] == 'n' ? ' ' : j[i]; } else v += j[i];
  }
  return v;
}
static void read_t3k(const std::string& dir, LibEntry& e) {
  FILE* f = fopen((dir + "/tone3000.json").c_str(), "r");
  if (!f) return;
  std::string j;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0 && j.size() < 65536) j.append(buf, n);
  fclose(f);
  if (json_str(j, "source") != "TONE3000") return;
  e.t3k = true;
  e.t3k_title = json_str(j, "title");
  e.t3k_gear = json_str(j, "gear");
  e.t3k_format = json_str(j, "format");
  e.t3k_creator = json_str(j, "creator");
}

static void list_files(const std::string& base, const std::string& dir, const std::string& ext, int depth,
                       std::vector<LibEntry>& out) {
  if (depth >= Library::kInsideDepth) return;
  for (const std::string& n : names_in(dir)) {
    std::string p = dir + "/" + n;
    Kind k = kind_of(p);
    if (k == DIR_) { if (lower(n) != "deleted") list_files(base, p, ext, depth + 1, out); continue; }
    if (k != FILE_) continue;
    size_t dot = n.find_last_of('.');
    if (dot == std::string::npos || lower(n.substr(dot)) != ext) continue;
    std::string rel = p.substr(base.size() + 1);             // "Fender/Twin Clean.nam"
    rel = rel.substr(0, rel.size() - (n.size() - dot));      // without the extension
    LibEntry e;
    e.path = p;
    e.display = rel;
    e.file = n;
    size_t slash = rel.find_last_of('/');
    e.pack = slash == std::string::npos ? std::string() : rel.substr(0, slash);
    read_t3k(dir, e);
    out.push_back(e);
  }
}

static std::string canonical(const std::string& p) {
  char buf[PATH_MAX];
  return realpath(p.c_str(), buf) ? std::string(buf) : p;
}

std::vector<LibEntry> Library::scan_folder(const std::string& dir, const std::string& ext) {
  std::vector<LibEntry> out;
  list_files(dir, dir, ext, 0, out);
  std::sort(out.begin(), out.end(), [](const LibEntry& a, const LibEntry& b) {
    const std::string la = lower(a.display), lb = lower(b.display);
    return la != lb ? la < lb : a.path < b.path;
  });
  return out;
}

std::vector<LibEntry> Library::scan(const std::string& folder, const std::string& ext) {
  std::vector<std::string> dirs;
  const std::string own = plugin_dir();
  if (!own.empty() && kind_of(own) == DIR_) find_dirs(own, folder, 0, 1, dirs);
  const std::string media = media_root();
  if (kind_of(media) == DIR_) find_dirs(media, folder, 0, kSearchDepth, dirs);
  std::vector<std::string> uniq;                   // the plugin's folder is itself under /media: no duplicates
  for (auto& d : dirs) {
    std::string c = canonical(d);
    if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) uniq.push_back(c);
  }
  std::vector<LibEntry> out;
  const std::string lext = lower(ext);
  for (auto& d : uniq) list_files(d, d, lext, 0, out);
  std::sort(out.begin(), out.end(), [](const LibEntry& a, const LibEntry& b) {
    const std::string la = lower(a.display), lb = lower(b.display);
    return la != lb ? la < lb : a.path < b.path;   // same name in two places: a stable order
  });
  return out;
}

bool Library::remove(const LibEntry& e, std::string* err) {
  auto fail = [err](const char* why) { if (err) *err = why; return false; };
  const size_t dot = e.file.find_last_of('.');
  const std::string ext = dot == std::string::npos ? std::string() : lower(e.file.substr(dot));
  if (ext != ".nam" && ext != ".wav") return fail("Not a model or IR file");
  // the path must be <a library folder>/<display><ext>, as scan() lists it: nothing else is ever deleted
  const std::string tail = "/" + e.display + e.file.substr(dot);
  if (e.path.size() <= tail.size() || e.path.compare(e.path.size() - tail.size(), tail.size(), tail) != 0)
    return fail("Can't place the file");
  const std::string root = e.path.substr(0, e.path.size() - tail.size());   // .../models
  if (kind_of(e.path) != FILE_) return fail("File not found");
  if (unlink(e.path.c_str()) != 0) return fail("Can't delete the file");
  // its pack folder: no audio files left -> the pack's tone3000.json goes too, then the (empty) folder
  const std::string dir = e.path.substr(0, e.path.find_last_of('/'));
  if (dir != root) {
    bool audio_left = false;
    for (const std::string& n : names_in(dir)) {
      const size_t d2 = n.find_last_of('.');
      const std::string x = d2 == std::string::npos ? "" : lower(n.substr(d2));
      if (x == ".nam" || x == ".wav" || kind_of(dir + "/" + n) == DIR_) audio_left = true;
    }
    if (!audio_left) {
      unlink((dir + "/tone3000.json").c_str());
      rmdir(dir.c_str());                                  // only if empty (a README or other files keep it)
    }
  }
  return true;
}

int Library::find(const std::vector<LibEntry>& lib, const std::string& path, const std::string& display,
                  const std::string& file) {
  for (size_t i = 0; i < lib.size(); i++) if (!path.empty() && lib[i].path == path) return (int)i;
  for (size_t i = 0; i < lib.size(); i++) if (!display.empty() && lib[i].display == display) return (int)i;
  for (size_t i = 0; i < lib.size(); i++) if (!file.empty() && lib[i].file == file) return (int)i;
  return -1;
}

}  // namespace irrev
