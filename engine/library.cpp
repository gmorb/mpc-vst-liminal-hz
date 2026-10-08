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
#include <cstring>

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

struct DirEnt {
  std::string name;
  Kind kind;
};

// The entries of a directory (no ".", "..", or hidden ones such as macOS's "._name.wav" and ".Trashes"), with their
// kind. The kind comes from readdir's d_type, so walking a big card costs one getdents per folder instead of an
// lstat per entry (the slow part of a scan on an SD card); lstat is only the fallback for a filesystem that doesn't
// fill d_type in. Links are NONE, as with lstat. Sorted only on request: the walk that looks for reverbs/ folders
// doesn't need an order (scan() sorts what it finds).
static std::vector<DirEnt> entries_in(const std::string& dir, bool sorted) {
  std::vector<DirEnt> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (struct dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    DirEnt x;
    x.name = e->d_name;
    switch (e->d_type) {
      case DT_DIR: x.kind = DIR_; break;
      case DT_REG: x.kind = FILE_; break;
      case DT_UNKNOWN: x.kind = kind_of(dir + "/" + x.name); break;
      default: x.kind = NONE; break;           // a link, a socket, ...
    }
    out.push_back(std::move(x));
  }
  closedir(d);
  if (sorted) std::sort(out.begin(), out.end(), [](const DirEnt& a, const DirEnt& b) { return a.name < b.name; });
  return out;
}

// folders a card carries that never hold IRs and can be big: not walked
static bool skip_dir(const std::string& lname) {
  return lname == "deleted" || lname == "system volume information" || lname == "$recycle.bin" ||
         lname == "lost+found" || lname == "found.000";
}

// every directory named `name` (any case) under root, down to max_depth levels below it
static void find_dirs(const std::string& root, const std::string& name, int depth, int max_depth,
                      std::vector<std::string>& out) {
  if (depth >= max_depth) return;
  for (const DirEnt& n : entries_in(root, false)) {
    if (n.kind != DIR_) continue;
    const std::string ln = lower(n.name);
    if (skip_dir(ln)) continue;                           // never scanned (earlier versions moved files there)
    const std::string p = root + "/" + n.name;
    if (ln == name) { out.push_back(p); continue; }       // a models/ folder's contents are listed separately
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
  LibEntry t3k;                                  // tone3000.json is the folder's: read once, not once per file
  bool t3k_read = false;
  for (const DirEnt& de : entries_in(dir, true)) {
    const std::string& n = de.name;
    const std::string p = dir + "/" + n;
    if (de.kind == DIR_) { if (!skip_dir(lower(n))) list_files(base, p, ext, depth + 1, out); continue; }
    if (de.kind != FILE_) continue;
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
    if (!t3k_read) { read_t3k(dir, t3k); t3k_read = true; }
    if (t3k.t3k) {
      e.t3k = true;
      e.t3k_title = t3k.t3k_title;
      e.t3k_gear = t3k.t3k_gear;
      e.t3k_format = t3k.t3k_format;
      e.t3k_creator = t3k.t3k_creator;
    }
    out.push_back(std::move(e));
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

// ---- the folder index ---------------------------------------------------------------------------------------------
// Finding the reverbs/ folders means walking the cards, which is the slow part of a scan. Where they were found is
// kept in a small hidden file in the plugin's folder (".<folder>-index"), so the next start can list those folders
// at once (scan_quick) and let the full walk (scan) catch up behind it. The file is only a hint: every folder in it
// is checked before use, a missing or unwritable file just means no quick scan, and nothing depends on it.
static const char kIndexMagic[] = "liminal-hz folder index 1";

static std::string index_path(const std::string& folder) {
  const std::string own = Library::plugin_dir();
  return own.empty() ? std::string() : own + "/." + folder + "-index";
}

static std::vector<std::string> read_index(const std::string& folder) {
  std::vector<std::string> dirs;
  const std::string path = index_path(folder);
  FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
  if (!f) return dirs;
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
  fclose(f);
  std::vector<std::string> lines;
  for (size_t i = 0; i < text.size();) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    lines.push_back(text.substr(i, e - i));
    i = e + 1;
  }
  if (lines.size() < 2 || lines[0] != kIndexMagic || lines[1] != "media=" + Library::media_root()) return dirs;
  for (size_t i = 2; i < lines.size(); i++) {
    // only a folder that is still there and still has the right name
    const std::string& d = lines[i];
    const size_t slash = d.find_last_of('/');
    if (slash == std::string::npos || lower(d.substr(slash + 1)) != folder) continue;
    if (kind_of(d) == DIR_) dirs.push_back(d);
  }
  return dirs;
}

static void write_index(const std::string& folder, const std::vector<std::string>& dirs) {
  const std::string path = index_path(folder);
  if (path.empty()) return;
  std::string text = std::string(kIndexMagic) + "\nmedia=" + Library::media_root() + "\n";
  for (const std::string& d : dirs) text += d + "\n";
  FILE* old = fopen(path.c_str(), "r");            // unchanged: don't touch the card
  if (old) {
    std::string cur;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, old)) > 0 && cur.size() <= text.size()) cur.append(buf, n);
    fclose(old);
    if (cur == text) return;
  }
  const std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return;                                  // a read-only folder: the hint is simply not kept
  const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  const bool closed = fclose(f) == 0;
  if (!ok || !closed || rename(tmp.c_str(), path.c_str()) != 0) unlink(tmp.c_str());
}

static std::vector<std::string> unique_dirs(const std::vector<std::string>& dirs) {
  std::vector<std::string> uniq;                   // the plugin's folder is itself under /media: no duplicates
  for (auto& d : dirs) {
    std::string c = canonical(d);
    if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) uniq.push_back(c);
  }
  return uniq;
}

static std::vector<LibEntry> list_dirs(const std::vector<std::string>& dirs, const std::string& ext) {
  std::vector<LibEntry> out;
  const std::string lext = lower(ext);
  for (auto& d : dirs) list_files(d, d, lext, 0, out);
  std::sort(out.begin(), out.end(), [](const LibEntry& a, const LibEntry& b) {
    const std::string la = lower(a.display), lb = lower(b.display);
    return la != lb ? la < lb : a.path < b.path;   // same name in two places: a stable order
  });
  return out;
}

static std::vector<std::string> own_dirs(const std::string& folder) {
  std::vector<std::string> dirs;
  const std::string own = Library::plugin_dir();
  if (!own.empty() && kind_of(own) == DIR_) find_dirs(own, folder, 0, 1, dirs);
  return dirs;
}

std::vector<LibEntry> Library::scan(const std::string& folder, const std::string& ext) {
  std::vector<std::string> dirs = own_dirs(folder);
  const std::string media = media_root();
  if (kind_of(media) == DIR_) find_dirs(media, folder, 0, kSearchDepth, dirs);
  const std::vector<std::string> uniq = unique_dirs(dirs);
  write_index(folder, uniq);                       // for the next start's scan_quick
  return list_dirs(uniq, ext);
}

bool Library::scan_quick(const std::string& folder, const std::string& ext, std::vector<LibEntry>* out) {
  std::vector<std::string> dirs = read_index(folder);
  if (dirs.empty()) return false;                  // no (usable) index yet: only the full walk can say
  for (auto& d : own_dirs(folder)) dirs.push_back(d);
  *out = list_dirs(unique_dirs(dirs), ext);
  return true;
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
    for (const DirEnt& de : entries_in(dir, false)) {
      const size_t d2 = de.name.find_last_of('.');
      const std::string x = d2 == std::string::npos ? "" : lower(de.name.substr(d2));
      if (x == ".nam" || x == ".wav" || de.kind == DIR_) audio_left = true;
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
