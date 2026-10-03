// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// library_test.cpp -- Library::scan on a made-up /media tree (MPCNAM_MEDIA) with the cases real cards have.
#include <cstdio>
#include <cstdlib>
#include <strings.h>
#include <filesystem>
#include <fstream>
#include "library.h"
using namespace irrev;
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static void touch(const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "x"; }
static bool has(const std::vector<LibEntry>& v, const std::string& d) {
  for (auto& e : v) if (e.display == d) return true;
  return false;
}
int main() {
  fs::path m = fs::temp_directory_path() / "mpcnam_media";
  fs::remove_all(m);
  touch(m / "662522/models/Fender/Twin Clean.nam");                 // top of a card, with a category folder
  touch(m / "662522/models/Marshall JCM800.nam");
  touch(m / "662522/models/._Marshall JCM800.nam");                 // macOS AppleDouble: skipped
  touch(m / "662522/models/notes.txt");                             // not a model
  touch(m / "662522/NAM/irs/Celestion V30.wav");                    // inside a NAM/ folder
  touch(m / "662522/NAM/irs/Greenback.WAV");                        // capital extension
  touch(m / "az01-internal/Stuff/IRs/Mesa/4x12 OS.wav");            // capitals, internal drive, category
  touch(m / "az01-internal/.Trashes/models/deleted.nam");           // hidden folder: skipped
  touch(m / "az01-internal/a/b/c/models/too deep.nam");             // models/ at depth 5: beyond the search
  touch(m / "az01-internal/a/b/models/deep enough.nam");            // models/ at depth 4: found
  touch(m / "sdb1/models/Marshall JCM800.nam");                     // same name on another card
  fs::create_directory_symlink(m, m / "662522/models/loop");        // a link back to the root: not followed
  setenv("MPCNAM_MEDIA", m.c_str(), 1);

  auto models = Library::scan("models", ".nam");
  auto irs = Library::scan("irs", ".wav");
  printf("models:"); for (auto& e : models) printf(" [%s]", e.display.c_str()); printf("\n");
  printf("irs:   "); for (auto& e : irs) printf(" [%s]", e.display.c_str()); printf("\n");
  CHECK(has(models, "Fender/Twin Clean") && has(models, "Marshall JCM800"), "models at the top of a card, with categories");
  CHECK(has(models, "deep enough") && !has(models, "too deep"), "models/ folders down to depth %d, not below", Library::kSearchDepth);
  CHECK(!has(models, "deleted") && !has(models, "._Marshall JCM800") && !has(models, "notes"), "hidden files and folders, and other file types, are skipped");
  CHECK(has(irs, "Celestion V30") && has(irs, "Greenback") && has(irs, "Mesa/4x12 OS"), "irs/ in any case and place, .wav in any case");
  int twins = 0; for (auto& e : models) twins += e.display == "Marshall JCM800";
  CHECK(twins == 2, "the same name on two cards: both listed (%d)", twins);
  CHECK(models.size() == 4, "no duplicates, no loops (%zu models: 2 cards + internal)", models.size());
  bool sorted = true; for (size_t i = 1; i < models.size(); i++) if (models[i - 1].display > models[i].display && strcasecmp(models[i-1].display.c_str(), models[i].display.c_str()) > 0) sorted = false;
  CHECK(sorted, "sorted by folder, then name");
  // finding a saved choice again
  int i = Library::find(models, "/gone/Fender/Twin Clean.nam", "Fender/Twin Clean", "Twin Clean.nam");
  CHECK(i >= 0 && models[i].display == "Fender/Twin Clean", "a saved model whose card moved is found by its folder and name");
  i = Library::find(irs, "/nowhere/V30.wav", "Old/Celestion V30", "Celestion V30.wav");
  CHECK(i >= 0 && irs[i].file == "Celestion V30.wav", "one moved to another folder is found by its file name");
  CHECK(Library::find(irs, "/x.wav", "x", "x.wav") == -1, "a missing file is reported as missing");
  // DELETE: Library::remove deletes a listed .nam/.wav permanently (and a TONE3000 pack's leftovers)
  {
    fs::path t = fs::temp_directory_path() / "mpcnam_remove";
    fs::remove_all(t);
    touch(t / "card/models/Fender/Twin.nam"); touch(t / "card/models/Fender/Deluxe.nam");
    touch(t / "card/models/TONE3000/Super Reverb/Edge.nam");
    std::ofstream(t / "card/models/TONE3000/Super Reverb/tone3000.json") << "{\"source\": \"TONE3000\", \"title\": \"Super Reverb\"}";
    setenv("MPCNAM_MEDIA", t.c_str(), 1);
    auto lib = Library::scan("models", ".nam");
    CHECK(lib.size() == 3, "before: %zu models", lib.size());
    std::string err;
    int k = Library::find(lib, "", "Fender/Twin", "");
    bool ok = k >= 0 && Library::remove(lib[k], &err);
    CHECK(ok && !fs::exists(t / "card/models/Fender/Twin.nam") && !fs::exists(t / "card/Deleted") &&
          fs::exists(t / "card/models/Fender/Deluxe.nam"), "a file is deleted (nothing kept anywhere), its neighbour stays");
    k = Library::find(lib, "", "TONE3000/Super Reverb/Edge", "");
    ok = k >= 0 && Library::remove(lib[k], &err);
    CHECK(ok && !fs::exists(t / "card/models/TONE3000/Super Reverb"), "a TONE3000 pack's last file: its tone3000.json and folder go too");
    LibEntry fake = lib[0]; fake.file = "notes.txt"; fake.path = (t / "card/models/notes.txt").string(); fake.display = "notes";
    touch(t / "card/models/notes.txt");
    CHECK(!Library::remove(fake, &err) && fs::exists(t / "card/models/notes.txt"), "only .nam/.wav files are ever deleted: [%s]", err.c_str());
    LibEntry outside = lib[0]; outside.path = "/etc/hostname.nam";
    CHECK(!Library::remove(outside, &err), "a path that isn't its library folder's is refused: [%s]", err.c_str());
    lib = Library::scan("models", ".nam");
    CHECK(lib.size() == 1 && lib[0].display == "Fender/Deluxe", "after: %zu model(s)", lib.size());
    fs::remove_all(t);
  }
  fs::remove_all(m);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
