// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* library.h -- the models (.nam) and cabinet IRs (.wav) a portable plugin can see (mpc-vst-nam-a2).
 *
 * Looks for folders named "models" and "irs" (any case) inside the plugin's own folder and anywhere under /media
 * (every memory card and the internal drive), down to kSearchDepth levels, and lists the matching files inside
 * them (with their subfolders, which act as categories), sorted by folder then name. Links are not followed,
 * hidden files (names starting with ".", e.g. macOS's "._name.wav") are skipped, and nothing here runs on the
 * audio thread: scanning touches the file system.
 */
#pragma once
#include <string>
#include <vector>

namespace irrev {

struct LibEntry {
  std::string path;      // absolute
  std::string display;   // relative to its models/ or irs/ folder, without the extension ("Fender/Twin Clean")
  std::string file;      // file name ("Twin Clean.nam")
  // the pack: the folder the file is in, relative to its models/ or irs/ folder ("" for loose files)
  std::string pack;
  // from tone3000.json beside the file (written by GET TONES): a TONE3000 tone's attribution
  bool t3k = false;
  std::string t3k_title, t3k_gear, t3k_format, t3k_creator;
};

class Library {
 public:
  static constexpr int kSearchDepth = 4;   // how deep under /media a models/ or irs/ folder may sit
  static constexpr int kInsideDepth = 4;   // how deep inside a models/ or irs/ folder files are listed

  // folder: "models" or "irs"; ext: ".nam" or ".wav"
  static std::vector<LibEntry> scan(const std::string& folder, const std::string& ext);
  // the files under one folder (its subfolders as packs), sorted the same way: e.g. the plugin's factory/ IRs
  static std::vector<LibEntry> scan_folder(const std::string& dir, const std::string& ext);

  // where to search: the plugin's own folder first, then media_root() (default "/media"; the environment variable
  // MPCNAM_MEDIA overrides it, for tests)
  static std::string plugin_dir();
  static std::string media_root();

  // the entry to use for something saved as (path, display, file): exact path, else the same relative display
  // path in any library folder, else the same file name; -1 if none
  static int find(const std::vector<LibEntry>& lib, const std::string& path, const std::string& display,
                  const std::string& file);

  // DELETE: removes the file permanently. Only a .nam or .wav file inside its scanned models/ (irs/, reverbs/)
  // folder, as listed by scan(). If its pack folder then holds no more audio files, the pack's tone3000.json
  // (TONE3000's attribution) goes too and the empty folder is removed. Not for the audio thread.
  static bool remove(const LibEntry& e, std::string* err);

};

}  // namespace irrev
