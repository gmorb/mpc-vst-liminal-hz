// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// presets_test.cpp -- engine/presets (My Presets files) on a made-up presets folder (MPCNAM_PRESETS).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "presets.h"
using namespace irrev;
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c, ...) do { const bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static float val(const PresetData& p, const std::string& k, float dflt = -999) {
  for (auto& kv : p.values) if (kv.first == k) return kv.second;
  return dflt;
}
int main() {
  fs::path d = fs::temp_directory_path() / "mpcnam_presets";
  fs::remove_all(d);
  setenv("MPCNAM_PRESETS", (d / "presets").c_str(), 1);

  CHECK(Presets::list().empty(), "no folder yet: an empty list, no crash");
  PresetData a;
  a.name = "Big Dark Hall";
  a.values = {{"mix", 42}, {"lowcut", 120.5f}, {"output", -3}, {"reverse", 1}};
  a.ir_src = 1; a.ir_path = "/media/card/reverbs/Halls/Big Hall.wav"; a.ir_display = "Halls/Big Hall"; a.ir_file = "Big Hall.wav";
  std::string err;
  CHECK(Presets::save(a, false, &err) && fs::exists(d / "presets/Big Dark Hall.lhzp"), "save creates the folder and <name>.lhzp");
  PresetData got;
  CHECK(Presets::load("Big Dark Hall", &got) && got.name == "Big Dark Hall" && val(got, "mix") == 42 &&
        val(got, "lowcut") == 120.5f && val(got, "output") == -3 && got.ir_src == 1 && got.ir_file == "Big Hall.wav" &&
        got.ir_display == "Halls/Big Hall" && got.ir_path == a.ir_path, "settings and the IR come back as saved");
  CHECK(Presets::load("big dark hall", &got), "a name is found in any case (a FAT card)");
  CHECK(!Presets::save(a, false, &err) && err == "That name is already used", "a name in use is refused: [%s]", err.c_str());
  a.values[0].second = 10;
  CHECK(Presets::save(a, true, &err) && Presets::load("Big Dark Hall", &got) && val(got, "mix") == 10, "overwrite replaces it");
  CHECK(!fs::exists(d / "presets/.Big Dark Hall.tmp"), "no temp file is left");

  PresetData none; none.name = "No IR"; none.values = {{"mix", 5}};
  CHECK(Presets::save(none, false, &err) && Presets::load("No IR", &got) && got.ir_file.empty() && got.ir_path.empty(), "a preset without an IR");

  // names
  CHECK(Presets::clean_name("  a/b\\c:d*e?f\"g<h>i|j ") == "a-b-c-d-e-f-g-h-i-j", "bad characters become '-', ends trimmed: [%s]", Presets::clean_name("  a/b\\c:d*e?f\"g<h>i|j ").c_str());
  CHECK(Presets::clean_name("...") == "" && Presets::clean_name("   ") == "" && Presets::clean_name("../../etc") == "-..-etc" ,
        "dots and spaces only is empty; no path escapes: [%s]", Presets::clean_name("../../etc").c_str());
  CHECK(Presets::clean_name(std::string("x\n\ty")) == "x--y", "control characters");
  std::string longname(100, 'a');
  CHECK(Presets::clean_name(longname).size() == Presets::kMaxName, "long names are cut to %zu", Presets::kMaxName);
  std::string u8 = std::string(39, 'a') + "\xC3\xA9\xC3\xA9";                      // 39 + two 2-byte letters
  const std::string cu = Presets::clean_name(u8);
  CHECK(cu.size() == 39 && (unsigned char)cu.back() == 'a', "a cut never splits a UTF-8 character (%zu bytes)", cu.size());
  CHECK(Presets::unique_name("Big Dark Hall") == "Big Dark Hall 2" && Presets::unique_name("big dark hall") == "big dark hall 2", "unique_name adds a number: [%s]", Presets::unique_name("Big Dark Hall").c_str());
  CHECK(Presets::unique_name("Fresh") == "Fresh" && Presets::unique_name("///") == "---" && Presets::unique_name("") == "My Preset", "unique_name: free names, odd ones");
  PresetData bad; bad.name = "..";
  CHECK(!Presets::save(bad, false, &err), "an empty name can't be saved: [%s]", err.c_str());

  // list, rename, delete
  PresetData z; z.name = "alpha"; z.values = {{"mix", 1}};
  Presets::save(z, false, &err);
  auto l = Presets::list();
  CHECK(l.size() == 3 && l[0].name == "alpha" && l[1].name == "Big Dark Hall" && l[2].name == "No IR", "listed by name, ignoring case (%zu)", l.size());
  std::ofstream(d / "presets/notes.txt") << "x";
  std::ofstream(d / "presets/junk.lhzp") << "not a preset";
  std::ofstream(d / "presets/._alpha.lhzp") << "liminal-hz-preset=1\nmix=1\n";
  fs::create_directory(d / "presets/folder.lhzp");
  CHECK(Presets::list().size() == 3, "other files, junk, hidden files and folders are skipped");
  CHECK(Presets::rename("alpha", "Beta Room", &err) && Presets::load("Beta Room", &got) && !Presets::exists("alpha"), "rename");
  CHECK(!Presets::rename("Beta Room", "No IR", &err) && err == "That name is already used", "rename onto another preset is refused: [%s]", err.c_str());
  CHECK(Presets::rename("Beta Room", "beta room", &err) && Presets::exists("Beta Room"), "rename that only changes case is fine");
  CHECK(!Presets::rename("nope", "x", &err) && err == "Preset not found", "rename of a missing preset: [%s]", err.c_str());
  CHECK(!Presets::rename("Beta Room", "///", &err) || Presets::exists("---"), "rename to an odd name is cleaned, never escapes the folder");
  CHECK(Presets::remove("No IR", &err) && !Presets::exists("No IR") && !fs::exists(d / "presets/No IR.lhzp"), "delete removes the file");
  CHECK(!Presets::remove("No IR", &err) && err == "Preset not found", "deleting twice: [%s]", err.c_str());
  CHECK(fs::exists(d / "presets/notes.txt"), "only preset files are ever deleted");
  // a file edited by hand
  std::ofstream(d / "presets/By Hand.lhzp") << "liminal-hz-preset=1\r\nmix=77\r\nbogus line\r\nwidth=nan\r\nir_file=x.wav\r\n";
  CHECK(Presets::load("By Hand", &got) && val(got, "mix") == 77 && val(got, "width") == -999 && got.ir_file == "x.wav", "a hand-edited file (CRLF, junk lines, nan) loads");
  fs::remove_all(d);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
