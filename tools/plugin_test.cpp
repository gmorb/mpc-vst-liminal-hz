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
       P_WAVE0, P_PLAYHEAD = P_WAVE0 + kCols, P_SOURCE, P_PRESET, P_SONICS, P_PRESET_OPEN, P_TOTAL };
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
    CHECK(html.find("Liminal Hz has partnered with TONE3000") != std::string::npos && html.find("Continue: reverb and space IRs") != std::string::npos,
          "the partnership splash, Continue to reverb IRs");
    if (system(("curl -s -L -o /dev/null '" + self + "/go?kind=space'").c_str()) != 0) printf("   (curl failed)\n");
    wait_for([&] { return idle(t) && disp(t, P_IR) == "Catacombs Far"; }, 60);
    CHECK(disp(t, P_IR) == "Catacombs Far" && disp(t, P_IR_PACK) == "Green-Wood Catacombs" && t->getP(t, P_IR_SRC) == 1 &&
          disp(t, P_IR_INFO).find("Space \xC2\xB7 IR \xC2\xB7 @tone3000") == 0,
          "downloaded (gears=space_outboard) and selected: [%s] [%s] [%s]", disp(t, P_IR).c_str(), disp(t, P_IR_PACK).c_str(), disp(t, P_IR_INFO).c_str());
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
    CHECK(fetch(base).find("has partnered with TONE3000") != std::string::npos && fetch(base + 1).find("has partnered with TONE3000") != std::string::npos,
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
    CHECK(disp(p3, P_T3K_INFO).find("Open ") == 0 && fetch(base).find("has partnered with TONE3000") != std::string::npos,
          "and opens again with the next press: [%s]", disp(p3, P_T3K_INFO).c_str());
    p3->d(p3, 1, 0, 0, nullptr, 0);
    unsetenv("MPCNAM_T3K_IDLE");
  }

  // browsing while audio runs on another thread
  AEffect* d = open_plugin();
  wait_for([&] { return idle(d); });
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
    if (k % 8 == 0) d->setP(d, P_RESCAN, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
  wait_for([&] { return idle(d); });
  run = false; audio.join();
  CHECK(sane, "24 browses, Length changes and rescans during %ld audio blocks: output always finite", (long)blocks);
  auto t0 = std::chrono::steady_clock::now();
  d->d(d, 1, 0, 0, nullptr, 0); a->d(a, 1, 0, 0, nullptr, 0);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 2000, "instances close promptly (%.0f ms for two)", ms);
  fs::remove_all(m1, ec); fs::remove_all(m2, ec);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
