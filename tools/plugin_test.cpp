// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// plugin_test.cpp <plugin.so> <wavs dir (make_test_wavs.py)> -- the plugin as MPC's host uses it, on a made-up
// /media (MPCNAM_MEDIA): reverbs/ folders, browsing and packs, mono/stereo/broken IRs, the reverb itself, Length,
// projects (also from a moved card), TONE3000 against tools/t3k_mock.py (when MPCNAM_T3K_API is set), browsing
// while audio runs on another thread, and closing.
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
namespace fs = std::filesystem;
struct AEffect;
typedef intptr_t (*hostcb)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
  int32_t magic; intptr_t (*d)(AEffect*, int32_t, int32_t, intptr_t, void*, float); void* p;
  void (*setP)(AEffect*, int32_t, float); float (*getP)(AEffect*, int32_t);
  int32_t numPrograms, numParams, numInputs, numOutputs, flags; intptr_t r1, r2; int32_t a, b, c; float io;
  void *obj, *user; int32_t uid, ver; void (*pr)(AEffect*, float**, float**, int32_t); void* pdr; char f[56];
};
static const int kCols = 12;                    // the SHAPE display's columns (ReverbIR::kCols)
static const int kPlay = 24;                    // the playhead's positions (ReverbEngine::kPlay)
enum { P_MIX, P_PREDELAY, P_LOWCUT, P_HIGHCUT, P_WIDTH, P_LENGTH, P_OUTPUT, P_IR, P_IR_PREV, P_IR_NEXT, P_IR_INFO,
       P_IR_PACK, P_IR_PACK_PREV, P_IR_PACK_NEXT, P_IR_SRC, P_IR_BROWSE, P_RESCAN, P_T3K_INFO, P_IR_DELETE,
       P_START, P_FADE_IN, P_FADE_OUT, P_STRETCH, P_REVERSE, P_FEEDBACK,
       P_WAVE0, P_PLAYHEAD = P_WAVE0 + kCols, P_SOURCE, P_PRESET, P_SONICS,
       P_MP_INFO, P_MP_SAVE, P_MP_DELETE, P_MP_PHONE, P_MENU, P_IR_LIST, P_MP_LIST, P_MENU_CLOSE, P_MENU_PREV, P_MENU_NEXT,
       P_MENU_TITLE, P_ROW1, P_ROW6 = P_ROW1 + 5, P_DECAY, P_PRESET_OPEN, P_TOTAL };
static const int kPresets = 14;                 // factory presets after Init (PRESET's options: Init + 14)
static int fails = 0;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static AEffect* (*entry)(hostcb);
static intptr_t host(AEffect*, int32_t op, int32_t, intptr_t, void*, float) { return op == 1 ? 2400 : 0; }
static std::string disp(AEffect* a, int i) { char b[256] = {0}; a->d(a, 7, i, 0, b, 0); return b; }
static bool wait_for(std::function<bool()> ok, double secs = 30) {
  for (int i = 0; i < secs * 50; i++) { if (ok()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  return ok();
}
static bool idle(AEffect* a) { auto s = disp(a, P_IR_INFO); return s != "Searching..." && s != "Loading..."; }
static AEffect* open_plugin() {
  AEffect* a = entry(host);
  a->d(a, 0, 0, 0, nullptr, 0); a->d(a, 10, 0, 0, nullptr, 44100.f); a->d(a, 11, 0, 128, nullptr, 0); a->d(a, 12, 0, 1, nullptr, 0);
  return a;
}
static void press(AEffect* a, int i) { a->setP(a, i, 1.0f); }
// rms of the wet part: a burst then silence; returns the tail's rms (after the burst), checks finiteness
static double tail_rms(AEffect* a, bool* finite) {
  std::vector<float> l(128), r(128), ol(128), orr(128);
  double acc = 0; long n = 0; bool fin = true; static long ph = 0;
  for (int b = 0; b < 300; b++) {
    for (int i = 0; i < 128; i++, ph++) l[i] = r[i] = b < 40 ? (float)(0.3 * std::sin(2 * M_PI * 220 * ph / 44100.0)) : 0.0f;
    float* in[2] = {l.data(), r.data()}; float* out[2] = {ol.data(), orr.data()};
    a->pr(a, in, out, 128);
    for (int i = 0; i < 128; i++) { if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) fin = false; if (b >= 60) { acc += ol[i] * ol[i]; n++; } }
  }
  if (finite) *finite = fin;
  return std::sqrt(acc / std::max(1L, n));
}

int main(int argc, char** argv) {
  if (argc < 3) { printf("usage: plugin_test <plugin.so> <wavs dir>\n"); return 2; }
  const std::string W = argv[2], tag = std::to_string((long)getpid());
  fs::path m1 = fs::temp_directory_path() / ("irrev_card1_" + tag), m2 = fs::temp_directory_path() / ("irrev_card2_" + tag);
  std::error_code ec; fs::remove_all(m1, ec); fs::remove_all(m2, ec);
  fs::create_directories(m1 / "662522/reverbs/Halls"); fs::create_directories(m1 / "662522/NAM/Reverbs/Rooms");
  fs::copy_file(W + "/stereo_ext_float.wav", m1 / "662522/reverbs/Halls/Big Hall.wav");
  fs::copy_file(W + "/pcm24.wav", m1 / "662522/reverbs/Halls/Small Hall.wav");
  fs::copy_file(W + "/ext_pcm24.wav", m1 / "662522/NAM/Reverbs/Rooms/Studio Room.wav");
  std::ofstream(m1 / "662522/reverbs/zz broken.wav") << "not a wav";
  setenv("MPCNAM_MEDIA", m1.c_str(), 1);
  setenv("MPCNAM_T3K_DEST", m1.c_str(), 1);
  const fs::path pdir = m1 / "my-presets";
  setenv("MPCNAM_PRESETS", pdir.c_str(), 1);

  void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!h) { printf("FAIL dlopen: %s\n", dlerror()); return 1; }
  entry = (AEffect * (*)(hostcb)) dlsym(h, "VSTPluginMain");
  AEffect* a = open_plugin();
  char name[64] = {0}, vendor[64] = {0}; a->d(a, 45, 0, 0, name, 0); a->d(a, 47, 0, 0, vendor, 0);
  CHECK(a->magic == 0x56737450 && a->numParams == P_TOTAL && a->numInputs == 2 && a->numOutputs == 2,
        "VST2 effect \"%s\" by \"%s\", %d parameters, stereo", name, vendor, (int)P_TOTAL);
  bool names = true; for (int i = 0; i < a->numParams; i++) { char n[64] = {0}; a->d(a, 8, i, 0, n, 0); if (!n[0]) names = false; }
  CHECK(names, "every parameter has a name");
  CHECK(disp(a, P_MIX) == "30%" && disp(a, P_HIGHCUT) == "12.0 kHz" && disp(a, P_LOWCUT) == "80 Hz" && disp(a, P_OUTPUT) == "0.0 dB",
        "values read naturally: [%s] [%s] [%s] [%s]", disp(a, P_MIX).c_str(), disp(a, P_LOWCUT).c_str(), disp(a, P_HIGHCUT).c_str(), disp(a, P_OUTPUT).c_str());
  { // Low Cut and High Cut turn on a log scale: the middle is 141 Hz / 4.5 kHz
    const float lc = a->getP(a, P_LOWCUT), hc = a->getP(a, P_HIGHCUT);
    CHECK(std::fabs(lc - 0.3544f) < 0.002f && std::fabs(hc - 0.8295f) < 0.002f, "defaults on the log scale: %.4f %.4f", lc, hc);
    a->setP(a, P_LOWCUT, 0.5f); a->setP(a, P_HIGHCUT, 0.5f);
    CHECK(disp(a, P_LOWCUT) == "141 Hz" && disp(a, P_HIGHCUT) == "4.5 kHz", "log knobs at the middle: [%s] [%s]", disp(a, P_LOWCUT).c_str(), disp(a, P_HIGHCUT).c_str());
    a->setP(a, P_LOWCUT, lc); a->setP(a, P_HIGHCUT, hc);
    CHECK(disp(a, P_LOWCUT) == "80 Hz" && disp(a, P_HIGHCUT) == "12.0 kHz", "and back: [%s] [%s]", disp(a, P_LOWCUT).c_str(), disp(a, P_HIGHCUT).c_str());
  }

  CHECK(wait_for([&] { return idle(a); }), "the drives are searched");
  { bool f0 = false; double dry_tail = tail_rms(a, &f0);
    CHECK(disp(a, P_IR) == "No IR" && disp(a, P_IR_INFO).find("Init") == 0 && disp(a, P_PRESET) == "Init" &&
          disp(a, P_SOURCE) == "Factory" && f0 && dry_tail < 1e-9,
          "a fresh plugin is Init: [%s] [%s], preset %s, source %s, the dry signal only (tail %.1e)", disp(a, P_IR).c_str(),
          disp(a, P_IR_INFO).c_str(), disp(a, P_PRESET).c_str(), disp(a, P_SOURCE).c_str(), dry_tail); }
  a->setP(a, P_SOURCE, 1.0f);                     // My IRs: the reverbs/ folders
  press(a, P_IR_NEXT);
  CHECK(wait_for([&] { return idle(a) && disp(a, P_IR) != "No IR"; }), "My IRs: NEXT picks the first IR");
  CHECK(disp(a, P_IR) == "Big Hall" && disp(a, P_IR_PACK) == "Halls" && disp(a, P_IR_INFO) == "IR 1 of 2 \xC2\xB7 Local \xC2\xB7 Stereo",
        "first IR: [%s] in [%s]: [%s]", disp(a, P_IR).c_str(), disp(a, P_IR_PACK).c_str(), disp(a, P_IR_INFO).c_str());
  { const std::string so = disp(a, P_SONICS);
    CHECK(so.rfind("decay ", 0) == 0 && (so.find(" s \xC2\xB7 ") != std::string::npos || so.find(" s+ \xC2\xB7 ") != std::string::npos) &&
          so.find("wide") != std::string::npos,
          "the room, in words: [%s]", so.c_str()); }
  bool fin;
  double wet = tail_rms(a, &fin);
  CHECK(fin && wet > 1e-4, "it reverberates: the tail after a burst has rms %.4f", wet);
  a->setP(a, P_MIX, 0.0f); double dry = tail_rms(a, nullptr); a->setP(a, P_MIX, 0.3f);
  CHECK(dry < 1e-6, "Mix 0: no tail (rms %.1e)", dry);
  press(a, P_IR_NEXT); wait_for([&] { return idle(a); });
  CHECK(disp(a, P_IR) == "Small Hall" && disp(a, P_IR_INFO) == "IR 2 of 2 \xC2\xB7 Local \xC2\xB7 Mono", "next: [%s] [%s]", disp(a, P_IR).c_str(), disp(a, P_IR_INFO).c_str());
  press(a, P_IR_PACK_NEXT); wait_for([&] { return idle(a); });
  CHECK(disp(a, P_IR) == "Studio Room" && disp(a, P_IR_PACK) == "Rooms", "NEXT PACK: [%s] in [%s] (EXTENSIBLE, no fact chunk)", disp(a, P_IR).c_str(), disp(a, P_IR_PACK).c_str());
  press(a, P_IR_PACK_NEXT); wait_for([&] { return idle(a); });
  CHECK(disp(a, P_IR) == "zz broken" && disp(a, P_IR_PACK) == "Loose files" && disp(a, P_IR_INFO) == "Not a WAV file",
        "a broken file says why: [%s] [%s]", disp(a, P_IR).c_str(), disp(a, P_IR_INFO).c_str());
  tail_rms(a, &fin);
  CHECK(fin, "audio keeps running");
  press(a, P_IR_PACK_NEXT); wait_for([&] { return idle(a); });
  CHECK(disp(a, P_IR) == "Big Hall", "NEXT PACK wraps around: [%s]", disp(a, P_IR).c_str());
  press(a, P_IR_PACK_PREV); wait_for([&] { return idle(a); });
  CHECK(disp(a, P_IR) == "zz broken", "PREV PACK wraps around: [%s]", disp(a, P_IR).c_str());
  press(a, P_IR_PACK_NEXT); wait_for([&] { return idle(a); });
  a->setP(a, P_LENGTH, 0.0f);                    // 5%: the IR is prepared again, shorter
  wait_for([&] { return idle(a); });
  double shortwet = tail_rms(a, nullptr);
  a->setP(a, P_LENGTH, 1.0f); wait_for([&] { return idle(a); });
  CHECK(disp(a, P_LENGTH) == "100%" && shortwet < wet, "Length 5%%: a shorter tail (rms %.4f vs %.4f)", shortwet, wet);

  { // IR shaping: Reverse prepares the IR again (in the background) and changes the sound
    double before = tail_rms(a, nullptr);
    a->setP(a, P_REVERSE, 1.0f);
    wait_for([&] { return idle(a); });
    bool rfin = false;
    double after = tail_rms(a, &rfin);
    CHECK(disp(a, P_REVERSE) == "On" && rfin && std::fabs(after - before) > 1e-6,
          "REVERSE on: the IR is prepared again; tail rms %.5f -> %.5f", before, after);
    a->setP(a, P_REVERSE, 0.0f); wait_for([&] { return idle(a); });
    CHECK(disp(a, P_STRETCH) == "100%" && disp(a, P_FADE_OUT) == "10%" && disp(a, P_FEEDBACK) == "0%",
          "shaping defaults: stretch %s, fade out %s, feedback %s", disp(a, P_STRETCH).c_str(), disp(a, P_FADE_OUT).c_str(), disp(a, P_FEEDBACK).c_str());
  }

  { // the waveform (kCols read-only columns: level x tone) and the playhead: a note starts it, it crosses the IR, then hides
    int nonzero = 0, first = (int)lroundf(a->getP(a, P_WAVE0) * 23) % 8, last = (int)lroundf(a->getP(a, P_WAVE0 + kCols - 1) * 23) % 8;
    for (int c = 0; c < kCols; c++) if (a->getP(a, P_WAVE0 + c) > 0) nonzero++;
    CHECK(nonzero >= 9 && first >= last && a->d(a, 26, P_WAVE0, 0, nullptr, 0) == 0,
          "the waveform: %d of 12 columns lit, %d at the start, %d at the end (decaying); not automatable", nonzero, first, last);
    a->setP(a, P_WAVE0, 1.0f);
    CHECK((int)lroundf(a->getP(a, P_WAVE0) * 23) % 8 == first, "the waveform is read-only (a host write is ignored)");
    std::vector<float> l(128, 0.0f), r(128, 0.0f), ol(128), orr(128);
    float* in[2] = {l.data(), r.data()}; float* out[2] = {ol.data(), orr.data()};
    for (int b = 0; b < 600; b++) a->pr(a, in, out, 128);                 // silence: no playhead
    const int idle_col = (int)lroundf(a->getP(a, P_PLAYHEAD) * kPlay);
    l[0] = r[0] = 0.8f; a->pr(a, in, out, 128); l[0] = r[0] = 0.0f;       // a note
    const int at_note = (int)lroundf(a->getP(a, P_PLAYHEAD) * kPlay);
    int max_col = at_note; bool moved = false;
    for (int b = 0; b < 2000; b++) { a->pr(a, in, out, 128); int c = (int)lroundf(a->getP(a, P_PLAYHEAD) * kPlay); if (c > at_note) moved = true; max_col = std::max(max_col, c); }
    const int after = (int)lroundf(a->getP(a, P_PLAYHEAD) * kPlay);
    CHECK(idle_col == 0 && at_note == 1 && moved && max_col == kPlay && after == 0,
          "the playhead: hidden (%d), a note starts it at 1 (%d), it crosses to %d, then hides (%d)", idle_col, at_note, max_col, after);
    // a MIDI note-on (when the host sends MIDI to the effect) starts it too, with no sound at all
    char can[] = "receiveVstMidiEvent";
    struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char midi[4]; char pad[4]; } on = {1, 32, 0, 0, 0, 0, {0x90, 60, 100, 0}, {0}};
    struct { int32_t n; intptr_t res; void* ev[2]; } evs = {1, 0, {&on, nullptr}};
    const intptr_t can_midi = a->d(a, 51, 0, 0, can, 0);
    a->d(a, 25, 0, 0, &evs, 0); a->pr(a, in, out, 128);
    const int at_midi = (int)lroundf(a->getP(a, P_PLAYHEAD) * kPlay);
    on.midi[2] = 0; for (int b = 0; b < 2000; b++) a->pr(a, in, out, 128);   // (a note-on of velocity 0 is a note-off)
    a->d(a, 25, 0, 0, &evs, 0); a->pr(a, in, out, 128);
    const int at_off = (int)lroundf(a->getP(a, P_PLAYHEAD) * kPlay);
    CHECK(can_midi == 1 && at_midi == 1 && at_off == 0, "MIDI: the plugin takes MIDI (%ld), a note-on starts the playhead (%d), a note-off doesn't (%d)",
          (long)can_midi, at_midi, at_off);
  }

  { // presets: the PRESET list (a popup on the page) loads a factory IR with its settings; Init clears it
    AEffect* pz = open_plugin();
    wait_for([&] { return idle(pz); });
    pz->setP(pz, P_PRESET_OPEN, 1.0f);                         // the list opens
    pz->setP(pz, P_PRESET, 3.0f / kPresets);                   // "Fluorescent Hall"
    wait_for([&] { return idle(pz) && disp(pz, P_IR) == "Fluorescent Hall"; }, 60);
    bool pf = false; double pt = tail_rms(pz, &pf);
    CHECK(pz->getP(pz, P_PRESET_OPEN) == 0 && disp(pz, P_PRESET) == "Fluorescent Hall" && disp(pz, P_SOURCE) == "Factory" &&
          disp(pz, P_IR) == "Fluorescent Hall" && disp(pz, P_IR_PACK) == "Spaces" && disp(pz, P_MIX) == "40%" &&
          disp(pz, P_PREDELAY) == "30 ms" && pf && pt > 1e-4,
          "a preset: the list closes, [%s] in [%s] [%s], mix %s, pre-delay %s, it reverberates (%.4f)", disp(pz, P_IR).c_str(),
          disp(pz, P_IR_PACK).c_str(), disp(pz, P_IR_INFO).c_str(), disp(pz, P_MIX).c_str(), disp(pz, P_PREDELAY).c_str(), pt);
    press(pz, P_IR_DELETE);
    CHECK(disp(pz, P_IR_INFO) == "Factory IRs can't be deleted" && fs::exists(std::string(getenv("MPCNAM_FACTORY")) + "/Spaces/Fluorescent_Hall.wav"),
          "DELETE refuses a factory IR: [%s]", disp(pz, P_IR_INFO).c_str());
    void* pc = nullptr; intptr_t pl = pz->d(pz, 23, 0, 0, &pc, 0); std::string ps((const char*)pc, pl);
    AEffect* pr = open_plugin();
    pr->d(pr, 24, 0, pl, (void*)ps.c_str(), 0);
    wait_for([&] { return idle(pr) && disp(pr, P_IR) == "Fluorescent Hall"; }, 60);
    CHECK(disp(pr, P_IR) == "Fluorescent Hall" && disp(pr, P_SOURCE) == "Factory" && pr->getP(pr, P_PRESET_OPEN) == 0,
          "a project on a factory IR reopens with it: [%s] (%s)", disp(pr, P_IR).c_str(), disp(pr, P_SOURCE).c_str());
    pr->d(pr, 1, 0, 0, nullptr, 0);
    pz->setP(pz, P_PRESET, 0.0f);                              // Init
    wait_for([&] { return idle(pz); });
    bool zf = false; double zt = tail_rms(pz, &zf);
    CHECK(disp(pz, P_IR) == "No IR" && disp(pz, P_MIX) == "30%" && disp(pz, P_PRESET) == "Init" && zf && zt < 1e-9,
          "Init: no IR, default settings, the dry signal only ([%s], mix %s, tail %.1e)", disp(pz, P_IR).c_str(), disp(pz, P_MIX).c_str(), zt);
    pz->d(pz, 1, 0, 0, nullptr, 0);
  }


  { // The list laid over the page: My Presets (SAVE, tap a row, DELETE, pages, projects, files renamed on a computer, a
    // missing IR, the phone page) and the IR List
    fs::remove_all(pdir, ec);
    AEffect* mp = open_plugin();
    auto info = [&] { return disp(mp, P_MP_INFO); };
    auto row = [&](AEffect* x, int r) { return disp(x, P_ROW1 + r); };
    auto on = [&](AEffect* x, int r) { return x->getP(x, P_ROW1 + r) > 0.5f; };
    auto menu = [&](AEffect* x) { return (int)lroundf(x->getP(x, P_MENU) * 2); };
    auto title = [&](AEffect* x) { return disp(x, P_MENU_TITLE); };
    auto tap = [&](AEffect* x, int r) { x->setP(x, P_ROW1 + r, 1.0f); };
    wait_for([&] { return info() != "Searching..."; }, 20);
    CHECK(menu(mp) == 0 && title(mp) == "" && row(mp, 0) == "" && disp(mp, P_PRESET) == "Init", "the list starts closed and empty: [%s]", title(mp).c_str());
    tap(mp, 0); mp->setP(mp, P_MENU, 1.0f); mp->setP(mp, P_MENU_NEXT, 1.0f);
    CHECK(menu(mp) == 0, "a row tap or a write to the menu state does nothing while it is closed");
    press(mp, P_MP_LIST);
    CHECK(menu(mp) == 2 && title(mp) == "No saved presets" && row(mp, 0) == "" && !on(mp, 0) && info() == "Tap SAVE to keep the current sound",
          "My Presets opens, nothing saved yet: [%s] [%s]", title(mp).c_str(), info().c_str());
    press(mp, P_MP_DELETE);
    CHECK(info() == "Pick a saved preset first", "DELETE with nothing selected says so: [%s]", info().c_str());
    mp->setP(mp, P_PRESET, 3.0f / kPresets);                                  // Fluorescent Hall: mix 40%, pre-delay 30 ms
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "Fluorescent Hall"; }, 60);
    mp->setP(mp, P_MIX, 0.7f);
    press(mp, P_MP_SAVE);
    wait_for([&] { return row(mp, 0) == "Fluorescent Hall"; }, 10);
    CHECK(row(mp, 0) == "Fluorescent Hall" && on(mp, 0) && info() == "Saved: Fluorescent Hall" && title(mp) == "Presets 1-1 of 1" &&
          disp(mp, P_PRESET) == "My: Fluorescent Hall" && fs::exists(pdir / "Fluorescent Hall.lhzp"),
          "SAVE: named from the IR, listed, the one in use, shown as the preset: [%s] [%s] [%s] [%s]", row(mp, 0).c_str(), info().c_str(), title(mp).c_str(), disp(mp, P_PRESET).c_str());
    press(mp, P_MP_SAVE);
    wait_for([&] { return row(mp, 1) == "Fluorescent Hall 2"; }, 10);
    CHECK(row(mp, 1) == "Fluorescent Hall 2" && on(mp, 1) && !on(mp, 0) && fs::exists(pdir / "Fluorescent Hall 2.lhzp") && title(mp) == "Presets 1-2 of 2",
          "a second SAVE gets a free name: [%s] [%s]", row(mp, 1).c_str(), title(mp).c_str());
    mp->setP(mp, P_PRESET, 0.0f);                                              // Init: no IR, defaults
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "No IR"; }, 20);
    CHECK(disp(mp, P_MIX) == "30%" && disp(mp, P_PRESET) == "Init" && !on(mp, 0) && !on(mp, 1) && info() == "Tap a preset to load it",
          "Init in between: mix %s, [%s], no row in use", disp(mp, P_MIX).c_str(), disp(mp, P_PRESET).c_str());
    tap(mp, 0);
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "Fluorescent Hall" && disp(mp, P_MIX) == "70%"; }, 60);
    { bool lf = false; double lt = tail_rms(mp, &lf);
      CHECK(on(mp, 0) && !on(mp, 1) && disp(mp, P_MIX) == "70%" && disp(mp, P_PREDELAY) == "30 ms" && disp(mp, P_IR) == "Fluorescent Hall" &&
            disp(mp, P_SOURCE) == "Factory" && info() == "IR: Fluorescent Hall" && menu(mp) == 2 && lf && lt > 1e-4,
            "a row tap loads it (settings and IR, it reverberates), the list stays open: mix %s [%s] (%.4f)", disp(mp, P_MIX).c_str(), info().c_str(), lt); }
    // the IR List: the My IRs files, a row tap selects like the arrows do
    mp->setP(mp, P_SOURCE, 1.0f);
    press(mp, P_IR_LIST);
    const std::string t0 = title(mp);
    CHECK(menu(mp) == 1 && t0.find("IRs 1-") == 0 && row(mp, 0).find("Big Hall") != std::string::npos && !on(mp, 0),
          "the IR List opens on the My IRs list: [%s] [%s]", t0.c_str(), row(mp, 0).c_str());
    tap(mp, 0);
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "Big Hall"; }, 20);
    CHECK(disp(mp, P_IR) == "Big Hall" && on(mp, 0) && menu(mp) == 1, "a row tap selects the IR: [%s]", disp(mp, P_IR).c_str());
    {   // a very long name scrolls, then stops at its end (and starts over when the page appears again)
      const std::string longn = "Zzz Very Long Impulse Response Name From A Big Open Industrial Space Capture Final Take v2";
      const fs::path lf = m1 / "662522/reverbs/Halls" / (longn + ".wav");
      fs::copy_file(W + "/pcm24.wav", lf);
      press(mp, P_RESCAN);
      auto find_row = [&]() { for (int r = 0; r < 6; r++) if (row(mp, r).find("Very Long") != std::string::npos || row(mp, r).find("Final Take") != std::string::npos) return r; return -1; };
      wait_for([&] { return idle(mp) && menu(mp) == 1 && find_row() >= 0; }, 30);
      press(mp, P_MENU_CLOSE); press(mp, P_IR_LIST);
      const int rr = find_row();
      CHECK(rr >= 0 && row(mp, rr).find("Halls") != std::string::npos && row(mp, rr).find("Zzz Very") != std::string::npos,
            "a long name starts whole: [%s]", rr >= 0 ? row(mp, rr).c_str() : "?");
      std::string prev;
      wait_for([&] { if (rr < 0) return true; const std::string c = row(mp, rr); const bool ok = c == prev && c.find("Zzz Very") == std::string::npos; prev = c;
                     if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(400)); return ok; }, 20);   // slid, and not moving any more
      const std::string end1 = rr >= 0 ? row(mp, rr) : "";
      std::this_thread::sleep_for(std::chrono::milliseconds(900));
      const std::string end2 = rr >= 0 ? row(mp, rr) : "";
      CHECK(rr >= 0 && end1 == end2 && end1.size() >= 8 && end1.size() < longn.size() + 10 && end1.compare(end1.size() - 2, 2, "v2") == 0,
            "it slides to the end and stops there: [%s] [%s]", end1.c_str(), end2.c_str());
      CHECK(row(mp, 0).find("Big Hall") != std::string::npos, "a short name never moves: [%s]", row(mp, 0).c_str());
      press(mp, P_MENU_CLOSE); press(mp, P_IR_LIST);
      CHECK(rr >= 0 && row(mp, rr).find("Zzz Very") != std::string::npos, "reopened, it starts over: [%s]", rr >= 0 ? row(mp, rr).c_str() : "?");
      fs::remove(lf);
      press(mp, P_RESCAN);
      wait_for([&] { return idle(mp) && find_row() < 0; }, 30);
    }
    mp->setP(mp, P_SOURCE, 0.0f);                                              // the factory list: all its IRs, paged
    { const int nf = atoi(title(mp).substr(title(mp).rfind(' ') + 1).c_str());
      CHECK(nf >= 14 && title(mp).find("IRs 1-6 of ") == 0 && row(mp, 0).find("\xC2\xB7") != std::string::npos && row(mp, 5) != "",
            "Factory: six rows to a page, 'pack \xC2\xB7 name': [%s] [%s] [%s]", title(mp).c_str(), row(mp, 0).c_str(), row(mp, 5).c_str());
      press(mp, P_MENU_NEXT);
      CHECK(title(mp).find("IRs 7-12 of ") == 0, "NEXT: the next page: [%s]", title(mp).c_str());
      press(mp, P_MENU_PREV); press(mp, P_MENU_PREV);
      const int pages = (nf + 5) / 6, lastn = nf - (pages - 1) * 6;
      char want[64]; snprintf(want, sizeof want, "IRs %d-%d of %d", (pages - 1) * 6 + 1, nf, nf);
      CHECK(title(mp) == want && row(mp, lastn - 1) != "" && (lastn == 6 || row(mp, lastn) == ""), "PREV wraps to the last page: [%s] (want %s)", title(mp).c_str(), want);
      press(mp, P_MENU_NEXT);
      CHECK(title(mp).find("IRs 1-6 of ") == 0, "NEXT wraps to the first: [%s]", title(mp).c_str()); }
    mp->setP(mp, P_SOURCE, 1.0f);
    wait_for([&] { return disp(mp, P_IR) == "Big Hall" && on(mp, 0); }, 10);
    mp->setP(mp, P_MIX, 0.2f);
    press(mp, P_MENU_CLOSE);
    CHECK(menu(mp) == 0 && title(mp) == "" && row(mp, 0) == "", "CLOSE");
    press(mp, P_MP_LIST);
    press(mp, P_MP_SAVE);
    wait_for([&] { return row(mp, 0) == "Big Hall" && on(mp, 0); }, 10);
    CHECK(row(mp, 0) == "Big Hall" && row(mp, 1) == "Fluorescent Hall" && row(mp, 2) == "Fluorescent Hall 2" && on(mp, 0) && fs::exists(pdir / "Big Hall.lhzp"),
          "a preset on a My IRs file, listed by name: [%s] [%s] [%s]", row(mp, 0).c_str(), row(mp, 1).c_str(), row(mp, 2).c_str());
    mp->setP(mp, P_PRESET, 0.0f);
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "No IR"; }, 20);
    tap(mp, 1);
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "Fluorescent Hall"; }, 60);
    tap(mp, 0);
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "Big Hall" && disp(mp, P_MIX) == "20%"; }, 60);
    CHECK(disp(mp, P_SOURCE) == "My IRs" && disp(mp, P_MIX) == "20%" && on(mp, 0) && !on(mp, 1),
          "a row finds its IR in My IRs: [%s] (%s), mix %s", disp(mp, P_IR).c_str(), disp(mp, P_SOURCE).c_str(), disp(mp, P_MIX).c_str());
    tap(mp, 3);                                                                // past the end of the list
    CHECK(disp(mp, P_IR) == "Big Hall" && on(mp, 0), "an empty row does nothing");
    // pages of presets (4 to a page)
    for (int k = 1; k <= 3; k++) { std::ofstream f(pdir / ("Zeta " + std::to_string(k) + ".lhzp")); f << "liminal-hz-preset=1\nmix=" << 10 + k << "\n"; }
    press(mp, P_RESCAN);
    wait_for([&] { return title(mp) == "Presets 1-4 of 6"; }, 15);
    CHECK(title(mp) == "Presets 1-4 of 6" && row(mp, 3) == "Zeta 1", "six presets: four on a page: [%s] [%s]", title(mp).c_str(), row(mp, 3).c_str());
    press(mp, P_MENU_NEXT);
    CHECK(title(mp) == "Presets 5-6 of 6" && row(mp, 0) == "Zeta 2" && row(mp, 1) == "Zeta 3" && row(mp, 2) == "" && !on(mp, 0), "NEXT: [%s] [%s]", title(mp).c_str(), row(mp, 0).c_str());
    tap(mp, 1);
    wait_for([&] { return disp(mp, P_MIX) == "13%"; }, 10);
    CHECK(disp(mp, P_MIX) == "13%" && on(mp, 1) && disp(mp, P_PRESET) == "My: Zeta 3", "a row on page 2 loads it: mix %s [%s]", disp(mp, P_MIX).c_str(), disp(mp, P_PRESET).c_str());
    press(mp, P_MENU_NEXT);
    CHECK(title(mp) == "Presets 1-4 of 6", "NEXT wraps: [%s]", title(mp).c_str());
    // DELETE: two taps, permanently; the list closes up
    press(mp, P_MENU_NEXT);                                                    // page 2 holds Zeta 3 (selected)
    press(mp, P_MP_DELETE);
    CHECK(info() == "Tap DELETE again to delete it permanently" && fs::exists(pdir / "Zeta 3.lhzp"), "one tap asks: [%s]", info().c_str());
    press(mp, P_MP_DELETE);
    wait_for([&] { return !fs::exists(pdir / "Zeta 3.lhzp") && title(mp) == "Presets 5-5 of 5"; }, 10);
    CHECK(!fs::exists(pdir / "Zeta 3.lhzp") && fs::exists(pdir / "Zeta 2.lhzp") && title(mp) == "Presets 5-5 of 5" && row(mp, 1) == "" && disp(mp, P_PRESET) != "My: Zeta 3",
          "two taps delete it, nothing else: [%s] [%s] [%s]", info().c_str(), title(mp).c_str(), disp(mp, P_PRESET).c_str());
    fs::remove(pdir / "Zeta 1.lhzp"); fs::remove(pdir / "Zeta 2.lhzp");      // removed on a computer
    press(mp, P_RESCAN);
    wait_for([&] { return title(mp) == "Presets 1-3 of 3"; }, 15);
    CHECK(title(mp) == "Presets 1-3 of 3", "files removed on a computer: gone after RESCAN: [%s]", title(mp).c_str());
    tap(mp, 0);                                                                // Big Hall (what the project below holds)
    wait_for([&] { return idle(mp) && disp(mp, P_IR) == "Big Hall"; }, 60);
    // a project keeps the selection (and the sound it holds); the list starts closed
    void* mc = nullptr; intptr_t ml = mp->d(mp, 23, 0, 0, &mc, 0); std::string ms((const char*)mc, ml);
    AEffect* m2p = open_plugin();
    m2p->d(m2p, 24, 0, ml, (void*)ms.c_str(), 0);
    wait_for([&] { return disp(m2p, P_PRESET) == "My: Big Hall" && idle(m2p); }, 30);
    press(m2p, P_MP_LIST);
    wait_for([&] { return row(m2p, 0) == "Big Hall" && on(m2p, 0); }, 10);
    CHECK(disp(m2p, P_PRESET) == "My: Big Hall" && disp(m2p, P_MIX) == "20%" && row(m2p, 0) == "Big Hall" && on(m2p, 0),
          "a project reopens on it: [%s] mix %s, listed and in use: [%s]", disp(m2p, P_PRESET).c_str(), disp(m2p, P_MIX).c_str(), row(m2p, 0).c_str());
    m2p->d(m2p, 1, 0, 0, nullptr, 0);
    // renamed on a computer: RESCAN picks it up
    fs::rename(pdir / "Big Hall.lhzp", pdir / "Warm Big Room.lhzp");
    press(mp, P_RESCAN);
    wait_for([&] { return row(mp, 2) == "Warm Big Room"; }, 15);
    CHECK(row(mp, 0) == "Fluorescent Hall" && row(mp, 2) == "Warm Big Room" && !fs::exists(pdir / "Big Hall.lhzp"), "a file renamed on a computer shows after RESCAN: [%s] [%s]", row(mp, 0).c_str(), row(mp, 2).c_str());
    // a preset whose IR is gone: the settings apply, and it says so
    { std::ofstream f(pdir / "Lost IR.lhzp"); f << "liminal-hz-preset=1\nmix=55\nir_src=1\nir_path=/gone/Nowhere.wav\nir_display=Nowhere\nir_file=Nowhere.wav\n"; }
    press(mp, P_RESCAN);
    wait_for([&] { return title(mp) == "Presets 1-4 of 4"; }, 15);
    tap(mp, 2);                                                                // sorted: Fluorescent Hall, Fluorescent Hall 2, Lost IR, Warm Big Room
    wait_for([&] { return row(mp, 2) == "Lost IR" && disp(mp, P_MIX) == "55%"; }, 10);
    CHECK(row(mp, 2) == "Lost IR" && disp(mp, P_MIX) == "55%" && info() == "IR not found: Nowhere.wav", "an IR that is gone: [%s] mix %s [%s]", row(mp, 2).c_str(), disp(mp, P_MIX).c_str(), info().c_str());
    // the phone page drives the live plugin
    press(mp, P_MP_PHONE);
    wait_for([&] { return info().find("Phone: ") == 0; }, 5);
    std::string web = info();
    CHECK(web.find("Phone: 127.0.0.1:") == 0 && web.size() > 8 && web.compare(web.size() - 8, 8, "/presets") == 0, "NAME ON PHONE shows the address: [%s]", web.c_str());
    const std::string base = "http://" + web.substr(7, web.size() - 7 - 8);
    auto curl = [&](const std::string& args) { return system(("curl -s -m 5 -o /dev/null " + args).c_str()); };
    mp->setP(mp, P_MIX, 0.9f);
    curl("--data-urlencode 'name=Typed On Phone' " + base + "/presets/save");
    wait_for([&] { return fs::exists(pdir / "Typed On Phone.lhzp") && title(mp) == "Presets 1-4 of 5"; }, 10);
    CHECK(fs::exists(pdir / "Typed On Phone.lhzp") && title(mp) == "Presets 1-4 of 5" && disp(mp, P_PRESET) == "My: Typed On Phone",
          "a name typed on the phone saves the current sound under it: [%s] [%s]", title(mp).c_str(), disp(mp, P_PRESET).c_str());
    mp->setP(mp, P_MIX, 0.1f);
    curl("--data-urlencode 'name=Warm Big Room' " + base + "/presets/load");
    wait_for([&] { return disp(mp, P_PRESET) == "My: Warm Big Room" && disp(mp, P_MIX) == "20%"; }, 20);
    CHECK(disp(mp, P_PRESET) == "My: Warm Big Room" && disp(mp, P_MIX) == "20%", "Load on the phone applies it: [%s] mix %s", disp(mp, P_PRESET).c_str(), disp(mp, P_MIX).c_str());
    curl("--data-urlencode 'from=Typed On Phone' --data-urlencode 'to=Phone Preset' " + base + "/presets/rename");
    wait_for([&] { return fs::exists(pdir / "Phone Preset.lhzp"); }, 10);
    CHECK(fs::exists(pdir / "Phone Preset.lhzp") && !fs::exists(pdir / "Typed On Phone.lhzp"), "Rename on the phone");
    curl("--data-urlencode 'name=Phone Preset' " + base + "/presets/delete");
    wait_for([&] { return !fs::exists(pdir / "Phone Preset.lhzp"); }, 10);
    CHECK(!fs::exists(pdir / "Phone Preset.lhzp"), "Delete on the phone");
    mp->d(mp, 1, 0, 0, nullptr, 0);
    fs::remove_all(pdir, ec);
  }

  // projects
  a->setP(a, P_MIX, 0.5f); a->setP(a, P_PREDELAY, 0.2f);
  press(a, P_IR_NEXT); wait_for([&] { return idle(a) && disp(a, P_IR) == "Small Hall"; });
  void* chunk = nullptr; intptr_t len = a->d(a, 23, 0, 0, &chunk, 0);
  std::string saved((const char*)chunk, len);
  AEffect* b = open_plugin();
  CHECK(b->d(b, 24, 0, len, (void*)saved.c_str(), 0) == 1, "a new instance accepts the project");
  wait_for([&] { return idle(b) && disp(b, P_IR) == "Small Hall"; });
  CHECK(disp(b, P_IR) == "Small Hall" && disp(b, P_MIX) == "50%" && disp(b, P_PREDELAY) == "50 ms",
        "and restores it: [%s], mix %s, pre-delay %s", disp(b, P_IR).c_str(), disp(b, P_MIX).c_str(), disp(b, P_PREDELAY).c_str());
  { // a project saved before the rename (state header "ir-reverb=1") still opens
    std::string old = "ir-reverb=1\n" + saved.substr(saved.find('\n') + 1);
    AEffect* o = open_plugin();
    CHECK(o->d(o, 24, 0, (intptr_t)old.size() + 1, (void*)old.c_str(), 0) == 1 && disp(o, P_MIX) == "50%",
          "a project from before the rename (\"ir-reverb=1\") opens: mix %s", disp(o, P_MIX).c_str());
    o->d(o, 1, 0, 0, nullptr, 0);
  }
  b->d(b, 1, 0, 0, nullptr, 0);
  fs::create_directories(m2 / "sdb1/Reverbs/Moved"); fs::copy_file(W + "/pcm24.wav", m2 / "sdb1/Reverbs/Moved/Small Hall.wav");
  setenv("MPCNAM_MEDIA", m2.c_str(), 1);
  AEffect* c = open_plugin();
  c->d(c, 24, 0, len, (void*)saved.c_str(), 0);
  wait_for([&] { return idle(c) && disp(c, P_IR) == "Small Hall"; });
  CHECK(disp(c, P_IR) == "Small Hall" && disp(c, P_IR_PACK) == "Moved", "on another card, in another folder: found by name ([%s] in [%s])", disp(c, P_IR).c_str(), disp(c, P_IR_PACK).c_str());
  c->d(c, 1, 0, 0, nullptr, 0);
  setenv("MPCNAM_MEDIA", m1.c_str(), 1);

  // TONE3000 (tools/test.sh runs tools/t3k_mock.py and sets MPCNAM_T3K_API)
  if (getenv("MPCNAM_T3K_API")) {
    AEffect* t = open_plugin();
    wait_for([&] { return idle(t); });
    press(t, P_IR_BROWSE);
    wait_for([&] { return disp(t, P_T3K_INFO).find("Open ") == 0; }, 5);
    std::string st = disp(t, P_T3K_INFO);
    CHECK(st.find("Open ") == 0, "BROWSE TONE3000 opens the phone page: [%s]", st.c_str());
    const std::string self = "http://127.0.0.1:" + std::string(getenv("MPCNAM_T3K_PORT"));
    std::string pagefile = (m1 / "page.html").string();
    if (system(("curl -s -o '" + pagefile + "' " + self + "/").c_str()) != 0) printf("   (curl failed)\n");
    std::string html; { std::ifstream f(pagefile); html.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
    CHECK(html.find("Liminal Hz can load impulse responses (IRs) from TONE3000") != std::string::npos && html.find("Continue: space, outboard, pedal and experimental IRs") != std::string::npos,
          "the partnership splash, Continue to reverb IRs");
    CHECK(html.find("href='/presets'") != std::string::npos && html.find("class=wm1>LIMINAL") != std::string::npos && html.find("--amber:#d9b77a") != std::string::npos,
          "the home page: the Liminal Hz look (wordmark, palette) and a button to My Presets");
    std::string ppage = (m1 / "presets.html").string();
    if (system(("curl -s -o '" + ppage + "' " + self + "/presets").c_str()) != 0) printf("   (curl failed)\n");
    std::string ph; { std::ifstream f(ppage); ph.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
    CHECK(ph.find("my presets") != std::string::npos && ph.find("href='/'") != std::string::npos && ph.find("class=wm1>LIMINAL") != std::string::npos,
          "the presets page: same look, and a way back home");
    const std::string fcode = (m1 / "font.code").string();
    if (system(("curl -s -o /dev/null -w '%{http_code}' " + self + "/font/regular.ttf > '" + fcode + "'").c_str()) != 0) printf("   (curl failed)\n");
    std::string fc; { std::ifstream f(fcode); std::getline(f, fc); }
    CHECK(fc == "200" || fc == "404", "the font route answers (%s: the font from the plugin folder, or the system font)", fc.c_str());
    if (system(("curl -s -L -o /dev/null '" + self + "/go?kind=space'").c_str()) != 0) printf("   (curl failed)\n");
    wait_for([&] { return idle(t) && disp(t, P_IR) == "Catacombs Far"; }, 60);
    CHECK(disp(t, P_IR) == "Catacombs Far" && disp(t, P_IR_PACK) == "Green-Wood Catacombs" && t->getP(t, P_IR_SRC) == 1 &&
          disp(t, P_IR_INFO).find("Space \xC2\xB7 IR \xC2\xB7 @tone3000") == 0,
          "downloaded (gears=space_outboard_pedal_experimental) and selected: [%s] [%s] [%s]", disp(t, P_IR).c_str(), disp(t, P_IR_PACK).c_str(), disp(t, P_IR_INFO).c_str());
    CHECK(fs::exists(m1 / "reverbs/TONE3000/Green-Wood Catacombs/tone3000.json"), "into the plugin's reverbs/TONE3000/<tone>/, with its attribution");
    t->d(t, 1, 0, 0, nullptr, 0);
  }

  // DELETE: one tap asks, a second within 4 s deletes the IR permanently
  {
    AEffect* x = open_plugin();
    wait_for([&] { return idle(x); });
    x->setP(x, P_SOURCE, 1.0f);
    for (int k = 0; k < 10 && disp(x, P_IR) != "Small Hall"; k++) { press(x, P_IR_NEXT); wait_for([&] { return idle(x); }); }
    press(x, P_IR_DELETE);
    CHECK(disp(x, P_IR) == "Small Hall" && disp(x, P_IR_INFO) == "Tap DELETE again to delete it permanently", "one tap asks: [%s]", disp(x, P_IR_INFO).c_str());
    press(x, P_IR_DELETE);
    wait_for([&] { return disp(x, P_IR_INFO) == "Deleted"; }, 5);
    CHECK(!fs::exists(m1 / "662522/reverbs/Halls/Small Hall.wav") && !fs::exists(m1 / "662522/Deleted"),
          "two taps: the IR is deleted permanently (info [%s])", disp(x, P_IR_INFO).c_str());
    wait_for([&] { return idle(x) && disp(x, P_IR) != "Small Hall"; }, 10);
    CHECK(disp(x, P_IR) != "Small Hall" && disp(x, P_IR) != "-", "the list moves on to [%s]", disp(x, P_IR).c_str());
    x->d(x, 1, 0, 0, nullptr, 0);
  }

  // the phone page's port: two instances each get one (the next port when it's taken); a page that closed itself
  // (idle) opens again
  if (getenv("MPCNAM_T3K_API")) {
    const int base = atoi(getenv("MPCNAM_T3K_PORT"));
    auto fetch = [&](int port) {
      std::string f = (fs::temp_directory_path() / ("port_page_" + std::to_string(port) + "_" + tag)).string();
      std::remove(f.c_str());
      if (system(("curl -s -m 5 -o '" + f + "' http://127.0.0.1:" + std::to_string(port) + "/").c_str()) != 0) return std::string();
      std::ifstream in(f); std::string h((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      std::remove(f.c_str());
      return h;
    };
    AEffect* p1 = open_plugin(); AEffect* p2 = open_plugin();
    wait_for([&] { return idle(p1) && idle(p2); });
    press(p1, P_IR_BROWSE); press(p2, P_IR_BROWSE);
    wait_for([&] { return disp(p1, P_T3K_INFO).find("Open ") == 0 && disp(p2, P_T3K_INFO).find("Open ") == 0; }, 5);
    const std::string s1 = disp(p1, P_T3K_INFO), s2 = disp(p2, P_T3K_INFO);
    CHECK(s1 == "Open 127.0.0.1:" + std::to_string(base) && s2 == "Open 127.0.0.1:" + std::to_string(base + 1),
          "two instances, two pages: [%s] [%s]", s1.c_str(), s2.c_str());
    CHECK(fetch(base).find("can load impulse responses (IRs) from TONE3000") != std::string::npos && fetch(base + 1).find("can load impulse responses (IRs) from TONE3000") != std::string::npos,
          "both pages answer");
    p2->d(p2, 1, 0, 0, nullptr, 0); p1->d(p1, 1, 0, 0, nullptr, 0);
    setenv("MPCNAM_T3K_IDLE", "1", 1);
    AEffect* p3 = open_plugin();
    wait_for([&] { return idle(p3); });
    press(p3, P_IR_BROWSE);
    wait_for([&] { return disp(p3, P_T3K_INFO) == "Page closed"; }, 6);
    CHECK(disp(p3, P_T3K_INFO) == "Page closed" && fetch(base).empty(), "an idle page closes itself: [%s]", disp(p3, P_T3K_INFO).c_str());
    press(p3, P_IR_BROWSE);
    wait_for([&] { return disp(p3, P_T3K_INFO).find("Open ") == 0; }, 5);
    CHECK(disp(p3, P_T3K_INFO).find("Open ") == 0 && fetch(base).find("can load impulse responses (IRs) from TONE3000") != std::string::npos,
          "and opens again with the next press: [%s]", disp(p3, P_T3K_INFO).c_str());
    p3->d(p3, 1, 0, 0, nullptr, 0);
    unsetenv("MPCNAM_T3K_IDLE");
  }

  // browsing while audio runs on another thread
  AEffect* d = open_plugin();
  fs::create_directories(m1 / "662522/reverbs/Long");
  fs::copy_file(W + "/long7s.wav", m1 / "662522/reverbs/Long/Long Hall.wav", fs::copy_options::overwrite_existing);
  d->setP(d, P_RESCAN, 1.0f);
  wait_for([&] { return idle(d); });
  {   // a 7 s IR (over the 5 s cap) at Decay 300%: this crashed the MPC (the reshaping overran its frames)
    d->setP(d, P_SOURCE, 1.0f);
    for (int k = 0; k < 40 && disp(d, P_IR) != "Long Hall"; k++) { press(d, P_IR_NEXT); wait_for([&] { return idle(d); }); }
    d->setP(d, P_DECAY, 1.0f);
    wait_for([&] { return idle(d); });
    for (int j = 0; j < 20; j++) { d->setP(d, P_DECAY, (j % 2) ? 1.0f : 0.6f + 0.02f * j); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    wait_for([&] { return idle(d); }, 20);
    d->setP(d, P_DECAY, 1.0f);
    wait_for([&] { return idle(d); }, 20);
    CHECK(disp(d, P_IR) == "Long Hall" && disp(d, P_DECAY) == "300%" && tail_rms(d, nullptr) > 0,
          "a 7 s IR at Decay 300%% (turned fast on the way): [%s] [%s] [%s]", disp(d, P_IR).c_str(), disp(d, P_DECAY).c_str(), disp(d, P_IR_INFO).c_str());
  }
  std::atomic<bool> run{true}, sane{true}; std::atomic<long> blocks{0};
  std::thread audio([&] {
    std::vector<float> l(128), ol(128), orr(128); long ph = 0;
    while (run) {
      for (int i = 0; i < 128; i++, ph++) l[i] = (float)(0.2 * std::sin(2 * M_PI * 110 * ph / 44100.0));
      float* in[2] = {l.data(), l.data()}; float* out[2] = {ol.data(), orr.data()};
      d->pr(d, in, out, 128);
      for (int i = 0; i < 128; i++) if (!std::isfinite(ol[i])) sane = false;
      blocks++;
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  });
  for (int k = 0; k < 24; k++) {
    d->setP(d, k % 3 ? P_IR_NEXT : P_IR_PACK_NEXT, 1.0f);
    d->setP(d, P_LENGTH, (k % 4) / 4.0f + 0.2f);
    for (int j = 0; j < 12; j++) { d->setP(d, P_DECAY, ((k * 12 + j) * 7 % 11) / 10.0f); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }   // Decay turned fast
    if (k % 8 == 0) d->setP(d, P_RESCAN, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
  wait_for([&] { return idle(d); });
  run = false; audio.join();
  CHECK(sane, "24 browses (a 7 s IR among them), Length and fast Decay changes and rescans during %ld audio blocks: output always finite", (long)blocks);
  auto t0 = std::chrono::steady_clock::now();
  d->d(d, 1, 0, 0, nullptr, 0); a->d(a, 1, 0, 0, nullptr, 0);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 2000, "instances close promptly (%.0f ms for two)", ms);
  fs::remove_all(m1, ec); fs::remove_all(m2, ec);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
