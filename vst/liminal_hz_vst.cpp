// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* liminal_hz_vst.cpp -- Liminal Hz as a Linux VST2 effect for MPC OS's plugin host (mpc-vst-liminal-hz).
 *
 * Hand-written VST2 ABI (no Steinberg SDK), parameters from vst/params.json (params.h by mpc-vst-plugins'
 * gen_vst.py; the order is the VST index and never changes). Threads, as NAM A2's wrapper:
 *   - the audio thread: ReverbEngine::process() and flag reads only (no locks, allocation or file access); the
 *     engine's convolvers run their tails on their own worker threads (engine/long_convolver.h);
 *   - the host's other threads: parameters (atomics) and browser requests (under `mu`);
 *   - one worker per instance: scans the drives for reverbs/ folders, prepares IRs (engine/reverb_engine.h), hands
 *     them over, frees the ones the engine let go of.
 * State is text: every parameter, and the IR by path, relative path and file name (found again if moved).
 */
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "library.h"
#include "params.h"
#include "reverb_engine.h"
#include "tone3000.h"

using namespace irrev;

// ---- VST2 ABI (hand-written; no Steinberg SDK) ------------------------------------------------------------------
struct AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
  int32_t magic;
  intptr_t (*dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
  void (*process)(AEffect*, float**, float**, int32_t);
  void (*setParameter)(AEffect*, int32_t, float);
  float (*getParameter)(AEffect*, int32_t);
  int32_t numPrograms, numParams, numInputs, numOutputs, flags;
  intptr_t resvd1, resvd2;
  int32_t initialDelay, realQualities, offQualities;
  float ioRatio;
  void *object, *user;
  int32_t uniqueID, version;
  void (*processReplacing)(AEffect*, float**, float**, int32_t);
  void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t);
  char future[56];
};
enum {
  effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
  effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23, effSetChunk = 24,
  effCanBeAutomated = 26, effGetPlugCategory = 35, effGetEffectName = 45, effGetVendorString = 47,
  effGetProductString = 48, effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42 };
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5 };
enum { kPlugCategEffect = 1 };

// ---- parameters (vst/params.json order) ----------------------------------------------------------------------------
enum {
  P_MIX, P_PREDELAY, P_LOWCUT, P_HIGHCUT, P_WIDTH, P_LENGTH, P_OUTPUT,
  P_IR, P_IR_PREV, P_IR_NEXT, P_IR_INFO, P_IR_PACK, P_IR_PACK_PREV, P_IR_PACK_NEXT, P_IR_SRC, P_IR_BROWSE,
  P_RESCAN, P_T3K_INFO,
  P_IR_DELETE,                                 // DELETE (two taps: deletes the file permanently)
  P_START, P_FADE_IN, P_FADE_OUT, P_STRETCH, P_REVERSE,   // IR shaping (prepared off the audio thread)
  P_FEEDBACK,
  P_WAVE0,                                     // the waveform: kCols read-only columns (tone x 8 + level), then the playhead
  P_PLAYHEAD = P_WAVE0 + ReverbIR::kCols,
  P_SOURCE,                                    // Factory / My IRs: which IRs the browser steps through
  P_PRESET,                                    // Init, or a factory preset (an IR with its settings)
  P_SONICS,                                    // read-only: what the IR sounds like ("decay 2.7 s · warm · wide")
  P_PRESET_OPEN,                               // the PRESET popup's open flag (added by the layout's popup)
  P_COUNT
};
static_assert(P_COUNT == NPARAMS, "vst/params.json and liminal_hz_vst.cpp disagree on the parameter list");
static const size_t kText = 24, kTextLong = 64;     // display text: as NAM A2 (64 for names, packs, details)

struct Browser {
  std::vector<LibEntry> lib;
  int want = -1;
  LibEntry loaded, saved;
  bool loading = false, loaded_stereo = false;
  IRShape loaded_shape;                        // the shape of the IR in the engine
  bool shape_valid = false;                    // false: nothing loaded (or the engine was rebuilt)
  std::string error;
  std::string armed_path;                      // DELETE: armed for this file until armed_until
  std::chrono::steady_clock::time_point armed_until, notice_until;
  bool delete_req = false;
  std::string notice;
};
static const int kArmSeconds = 4;

struct Inst {
  AEffect fx;
  audioMasterCallback master = nullptr;
  std::unique_ptr<ReverbEngine> eng;
  double rate = 44100.0;
  std::atomic<float> norm[P_COUNT];
  std::mutex mu;
  std::condition_variable cv;
  bool stop = false, scan_req = true, work_req = false, scanned = false, scanning = true;
  Browser ir;
  std::string select_after_scan;               // a TONE3000 download to select once it's scanned
  std::unique_ptr<Tone3000> t3k;
  std::thread worker;
  std::atomic<int> changed[P_COUNT];
  std::atomic<int> update_display{0};
  std::string chunk;
  std::vector<LibEntry> libs[2];               // 0: the factory IRs, 1: My IRs (reverbs/ folders)
  long late_seen = 0;                          // late tail blocks already noticed (host thread, under mu)
  std::chrono::steady_clock::time_point late_until;   // "Tail late" shows until then (5 s after the last one)
  std::atomic<int> wave[ReverbIR::kCols];      // the loaded IR's strands: level 0..7 + 8 x tint 0..2 (worker writes)
  std::string sonics;                          // the loaded IR's sonics, as words (under mu)
  int playhead_sent = 0, playhead_age = 0;     // the playhead as last reported (audio thread)
};

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float real_of(int i, float n) {
  const param_t& p = PARAMS[i];
  if (p.nopts > 1) return (float)lroundf(clamp01(n) * (p.nopts - 1));
  return p.min + (p.max - p.min) * clamp01(n);
}
// factory presets: each loads factory/<category>/<name>.wav with these settings (real values, as the page shows)
struct Preset {
  const char *name, *category;
  int mix, predelay, lowcut, highcut, width, length, output, start, fade_in, fade_out, stretch, reverse, feedback;
};
static const Preset kPresets[] = {
  {"Empty Mall", "Spaces", 35, 20, 90, 9000, 120, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Pool Rooms", "Spaces", 30, 8, 120, 14000, 110, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Fluorescent Hall", "Spaces", 40, 30, 60, 8000, 130, 100, 0, 0, 0, 12, 100, 0, 0},
  {"Backrooms", "Spaces", 28, 4, 80, 6000, 90, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Stairwell Flutter", "Echoes", 30, 0, 150, 9000, 110, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Tape Corridor", "Echoes", 32, 0, 120, 7000, 120, 100, 0, 0, 0, 8, 100, 0, 20},
  {"Dream Ping-Pong", "Echoes", 35, 0, 140, 12000, 160, 100, 0, 0, 0, 8, 100, 0, 15},
  {"Parking Slap", "Echoes", 30, 0, 100, 8000, 120, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Intercom Echo", "Echoes", 30, 0, 300, 4000, 100, 100, 0, 0, 0, 10, 100, 0, 25},
  {"Glass Void", "Strange", 30, 40, 200, 14000, 140, 100, 0, 0, 5, 15, 100, 0, 0},
  {"Reverse Bloom", "Strange", 40, 0, 120, 10000, 140, 100, 0, 0, 0, 4, 100, 0, 0},
  {"Sub Tunnel", "Strange", 30, 20, 30, 5000, 120, 100, 0, 0, 0, 12, 100, 0, 0},
  {"Shimmer Fog", "Strange", 38, 50, 250, 16000, 170, 100, 0, 0, 10, 15, 100, 0, 10},
  {"Liminal Hz", "Strange", 40, 35, 80, 11000, 150, 100, 0, 0, 5, 15, 100, 0, 12}
};
static const int kPresetCount = (int)(sizeof kPresets / sizeof kPresets[0]);

static bool is_display(int i) { return (i >= P_WAVE0 && i <= P_PLAYHEAD) || i == P_SONICS; }   // read-only, not automatable
static bool is_browser(int i) { return (i >= P_IR && i <= P_IR_BROWSE) || i == P_IR_DELETE; }

static void apply_param(Inst* w, int i) {      // plain parameter -> engine (atomics)
  ReverbParams& e = w->eng->params;
  const float v = real_of(i, w->norm[i]);
  switch (i) {
    case P_MIX: e.mix = v / 100.0f; break;
    case P_PREDELAY: e.predelay_ms = v; break;
    case P_LOWCUT: e.lowcut_hz = v; break;
    case P_HIGHCUT: e.highcut_hz = v; break;
    case P_WIDTH: e.width = v / 100.0f; break;
    case P_OUTPUT: e.output_db = v; break;
    case P_FEEDBACK: e.feedback = v / 100.0f; break;
    default: break;
  }
}

static float browser_norm(const Browser& b) {
  const int n = (int)b.lib.size();
  return (b.want < 0 || n < 2) ? 0.0f : (float)b.want / (float)(n - 1);
}
static float src_norm(const Browser& b) {
  return (b.want >= 0 && b.want < (int)b.lib.size() && b.lib[b.want].t3k) ? 1.0f : 0.0f;
}
static void requested(Browser& b) {            // holds mu: "Loading..." from the moment of the request
  b.loading = b.want >= 0 && b.want < (int)b.lib.size() && b.lib[b.want].path != b.loaded.path;
}
static void kick(Inst* w, bool scan) {
  w->scan_req = w->scan_req || scan;
  w->work_req = true;
  w->cv.notify_one();
}
static void browser_changed(Inst* w) { w->changed[P_IR] = 1; w->changed[P_IR_SRC] = 1; }

// ---- worker ------------------------------------------------------------------------------------------------------
static int source_of(Inst* w) { return real_of(P_SOURCE, w->norm[P_SOURCE]) > 0.5f ? 1 : 0; }

// factory IR files have no spaces ("Dream_Ping-Pong.wav", the release tool's rule); the page shows the name
static std::string factory_name(std::string s) { for (char& c : s) if (c == '_') c = ' '; return s; }
static std::string factory_file(std::string s) { for (char& c : s) if (c == ' ') c = '_'; return s; }

static void resolve(Browser& b) {              // after a scan: the saved or loaded entry again (holds mu)
  const LibEntry& s = !b.saved.path.empty() || !b.saved.file.empty() ? b.saved : b.loaded;
  int k = (s.path.empty() && s.file.empty()) ? -1 : Library::find(b.lib, s.path, s.display, s.file);
  if (k >= 0) { b.want = k; b.saved = LibEntry(); }
  else if (!b.saved.file.empty()) { b.want = -1; b.error = "Not found: " + b.saved.file; }
  else if (b.want >= (int)b.lib.size()) b.want = b.lib.empty() ? -1 : 0;
  // (no IR is picked by itself: a fresh plugin is "Init", the dry signal, until a preset or an IR is chosen)
}

static IRShape shape_of(Inst* w) {             // the IR shaping parameters (real values: %, on/off)
  IRShape s;
  s.start = real_of(P_START, w->norm[P_START]) / 100.0;
  s.length = real_of(P_LENGTH, w->norm[P_LENGTH]) / 100.0;
  s.fade_in = real_of(P_FADE_IN, w->norm[P_FADE_IN]) / 100.0;
  s.fade_out = real_of(P_FADE_OUT, w->norm[P_FADE_OUT]) / 100.0;
  s.stretch = real_of(P_STRETCH, w->norm[P_STRETCH]) / 100.0;
  s.reverse = real_of(P_REVERSE, w->norm[P_REVERSE]) > 0.5f;
  return s;
}
static bool is_shape(int i) {
  return i == P_LENGTH || i == P_START || i == P_FADE_IN || i == P_FADE_OUT || i == P_STRETCH || i == P_REVERSE;
}

static void load_ir(Inst* w) {
  std::unique_lock<std::mutex> lk(w->mu);
  Browser& b = w->ir;
  const IRShape shape = shape_of(w);
  if (b.want < 0 || b.want >= (int)b.lib.size()) { b.loading = false; return; }
  const LibEntry e = b.lib[b.want];
  if (e.path == b.loaded.path && b.shape_valid && shape == b.loaded_shape) { b.loading = false; return; }
  b.loading = true;
  const double rate = w->rate;
  w->update_display = 1;
  lk.unlock();
  std::string err;
  std::unique_ptr<ReverbIR> r = ReverbIR::load(e.path, rate, shape, &err);
  lk.lock();
  b.loading = false;
  if (!w->eng || w->rate != rate) return;
  if (!r) {
    b.error = err.size() < 32 && !err.empty() ? err : "Can't load " + e.file;
    b.loaded = LibEntry();
    b.loaded.path = e.path;                    // don't retry a broken file in a loop
    b.loaded_shape = shape;
    b.shape_valid = true;
    w->eng->set_ir(nullptr);
    for (int c = 0; c < ReverbIR::kCols; c++) { w->wave[c] = 0; w->changed[P_WAVE0 + c] = 1; }
    w->sonics.clear();
  } else {
    b.error.clear();
    b.loaded = e;
    b.loaded_shape = shape;
    b.shape_valid = true;
    b.loaded_stereo = r->stereo;
    for (int c = 0; c < ReverbIR::kCols; c++) { w->wave[c] = r->wave[c]; w->changed[P_WAVE0 + c] = 1; }
    w->sonics = r->sonics;                     // engine/reverb_engine.cpp describe(), at prep
    w->eng->set_ir(std::move(r));
  }
  w->update_display = 1;
}

static void worker_main(Inst* w) {
  for (;;) {
    bool scan;
    {
      std::unique_lock<std::mutex> lk(w->mu);
      w->cv.wait_for(lk, std::chrono::milliseconds(250), [w] { return w->stop || w->scan_req || w->work_req; });
      if (w->stop) return;
      scan = w->scan_req;
      w->scan_req = w->work_req = false;
      if (scan) { w->scanning = true; w->update_display = 1; }
    }
    if (scan) {
      auto mine = Library::scan("reverbs", ".wav");
      const char* fenv = getenv("MPCNAM_FACTORY");
      auto factory = Library::scan_folder(fenv ? fenv : Library::plugin_dir() + "/factory", ".wav");
      std::lock_guard<std::mutex> lk(w->mu);
      w->libs[0] = std::move(factory);
      w->libs[1] = std::move(mine);
      w->ir.lib = w->libs[source_of(w)];
      resolve(w->ir);
      if (!w->select_after_scan.empty()) {     // a TONE3000 download (in My IRs): show that list and select it
        w->norm[P_SOURCE] = 1.0f;
        w->changed[P_SOURCE] = 1;
        w->ir.lib = w->libs[1];
        int k = Library::find(w->ir.lib, w->select_after_scan, "", "");
        if (k >= 0) { w->ir.want = k; w->ir.saved = LibEntry(); w->ir.error.clear(); }
        w->select_after_scan.clear();
      }
      requested(w->ir);
      w->scanned = true;
      w->scanning = false;
      browser_changed(w);
      w->update_display = 1;
    }
    {                                          // DELETE: delete the file (permanently), then rescan
      LibEntry victim;
      bool go = false;
      {
        std::lock_guard<std::mutex> lk(w->mu);
        if (w->ir.delete_req) {
          w->ir.delete_req = false;
          if (w->ir.want >= 0 && w->ir.want < (int)w->ir.lib.size()) { victim = w->ir.lib[w->ir.want]; go = true; }
        }
      }
      if (go) {
        std::string err;
        const bool ok = Library::remove(victim, &err);     // permanent
        std::lock_guard<std::mutex> lk(w->mu);
        if (ok) {
          w->ir.notice = "Deleted";
          w->ir.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
          w->ir.error.clear();
          w->ir.loaded = LibEntry();
          w->scan_req = w->work_req = true;
          w->scanning = true;
        } else {
          w->ir.error = err;
        }
        w->update_display = 1;
      }
      std::lock_guard<std::mutex> lk(w->mu);   // an armed DELETE or a notice ran out: refresh the info line
      const auto now = std::chrono::steady_clock::now();
      if (!w->ir.armed_path.empty() && now >= w->ir.armed_until) { w->ir.armed_path.clear(); w->update_display = 1; }
      if (!w->ir.notice.empty() && now >= w->ir.notice_until) { w->ir.notice.clear(); w->update_display = 1; }
    }
    load_ir(w);
    std::lock_guard<std::mutex> lk(w->mu);
    if (w->eng) w->eng->collect();
  }
}

// ---- TONE3000 (host thread, without mu) ----------------------------------------------------------------------------
static void open_tones(Inst* w) {
  Tone3000* t;
  {
    std::lock_guard<std::mutex> lk(w->mu);
    t = w->t3k.get();
  }
  if (!t) {
    std::unique_ptr<Tone3000> made(new Tone3000([w](const std::string& path, const std::string&) {
      std::lock_guard<std::mutex> lk(w->mu);
      w->select_after_scan = path;
      w->scanning = true;
      kick(w, true);
      w->update_display = 1;
    }));
    std::lock_guard<std::mutex> lk(w->mu);
    if (!w->t3k) w->t3k = std::move(made);
    t = w->t3k.get();
  }
  t->start("space");
  w->update_display = 1;
}

// ---- VST callbacks -------------------------------------------------------------------------------------------------
static void set_real(Inst* w, int i, float real) {   // a plain parameter by its real value (presets)
  const param_t& p = PARAMS[i];
  w->norm[i] = p.nopts > 1 ? clamp01(real / (p.nopts - 1)) : (p.max > p.min ? clamp01((real - p.min) / (p.max - p.min)) : 0);
  apply_param(w, i);
  w->changed[i] = 1;
}

// PRESET picked (k = 0: Init): settings, then the factory IR (or none); closes the popup
static void apply_preset(Inst* w, int k) {
  if (k < 0 || k > kPresetCount) return;
  w->norm[P_PRESET] = (float)k / kPresetCount;
  w->norm[P_PRESET_OPEN] = 0;                  // a pick closes the list
  w->changed[P_PRESET] = 1;
  w->changed[P_PRESET_OPEN] = 1;
  if (k == 0) {                                // Init: every setting at its default, no IR
    for (int i = 0; i < P_COUNT; i++)
      if (!is_browser(i) && !is_display(i) && !PARAMS[i].momentary && i != P_PRESET && i != P_PRESET_OPEN &&
          i != P_RESCAN && i != P_T3K_INFO && i != P_SOURCE) {
        w->norm[i] = PARAMS[i].def; apply_param(w, i); w->changed[i] = 1;
      }
    std::lock_guard<std::mutex> lk(w->mu);
    Browser& b = w->ir;
    b.want = -1; b.saved = LibEntry(); b.loaded = LibEntry(); b.error.clear(); b.shape_valid = false; b.loading = false;
    if (w->eng) w->eng->set_ir(nullptr);
    for (int c = 0; c < ReverbIR::kCols; c++) { w->wave[c] = 0; w->changed[P_WAVE0 + c] = 1; }
    w->sonics.clear();
    browser_changed(w);
    w->update_display = 1;
    return;
  }
  const Preset& p = kPresets[k - 1];
  const int vals[][2] = {{P_MIX, p.mix}, {P_PREDELAY, p.predelay}, {P_LOWCUT, p.lowcut}, {P_HIGHCUT, p.highcut},
                         {P_WIDTH, p.width}, {P_LENGTH, p.length}, {P_OUTPUT, p.output}, {P_START, p.start},
                         {P_FADE_IN, p.fade_in}, {P_FADE_OUT, p.fade_out}, {P_STRETCH, p.stretch},
                         {P_REVERSE, p.reverse}, {P_FEEDBACK, p.feedback}};
  for (const auto& kv : vals) set_real(w, kv[0], (float)kv[1]);
  set_real(w, P_SOURCE, 0);                    // the factory list
  std::lock_guard<std::mutex> lk(w->mu);
  Browser& b = w->ir;
  b.lib = w->libs[0];
  b.want = Library::find(b.lib, "", std::string(p.category) + "/" + factory_file(p.name), "");
  b.saved = LibEntry();
  b.error = b.want < 0 ? std::string("Factory IR missing: ") + p.name : std::string();
  requested(b);
  kick(w, false);
  browser_changed(w);
  w->update_display = 1;
}

static void set_parameter(AEffect* e, int32_t i, float v) {
  Inst* w = (Inst*)e->object;
  if (i < 0 || i >= P_COUNT) return;
  v = clamp01(v);
  if (i == P_RESCAN || i == P_T3K_INFO) {
    if (i == P_RESCAN && v >= 0.5f) {
      std::lock_guard<std::mutex> lk(w->mu);
      w->scanning = true;
      kick(w, true);
      w->changed[P_RESCAN] = 1;
      w->update_display = 1;
    }
    return;
  }
  if (is_display(i)) return;                   // read-only: what the plugin shows
  if (i == P_PRESET) { apply_preset(w, (int)lroundf(v * kPresetCount)); return; }
  if (i == P_SOURCE) {                         // the other list; what plays stays (found there if it's in it)
    w->norm[i] = v;
    std::lock_guard<std::mutex> lk(w->mu);
    Browser& b = w->ir;
    b.lib = w->libs[source_of(w)];
    b.want = b.loaded.path.empty() ? -1 : Library::find(b.lib, b.loaded.path, "", "");
    b.error.clear();
    browser_changed(w);
    w->update_display = 1;
    return;
  }
  if (!is_browser(i)) {
    w->norm[i] = v;
    apply_param(w, i);
    if (is_shape(i)) {                         // a new shape: the IR is prepared again (crossfaded in)
      std::lock_guard<std::mutex> lk(w->mu);
      if (w->ir.want >= 0) w->ir.loading = true;
      kick(w, false);
    }
    w->update_display = 1;
    return;
  }
  bool browse = false;
  {
    std::lock_guard<std::mutex> lk(w->mu);
    Browser& b = w->ir;
    const int n = (int)b.lib.size();
    if (i == P_IR_SRC || i == P_IR_INFO || i == P_IR_PACK) return;   // read-only
    if (i == P_IR) {
      if (n > 0) {
        b.want = n > 1 ? (int)lroundf(v * (n - 1)) : 0;
        b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false);
        w->changed[P_IR_SRC] = 1;
      }
    } else if (i == P_IR_PREV || i == P_IR_NEXT) {
      if (v >= 0.5f && n > 0) {
        if (b.want < 0) b.want = i == P_IR_NEXT ? 0 : n - 1;   // from "no IR": the first / the last
        else b.want = ((b.want + (i == P_IR_NEXT ? 1 : -1)) % n + n) % n;
        b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false);
        browser_changed(w);
      }
      w->changed[i] = 1;
    } else if (i == P_IR_PACK_PREV || i == P_IR_PACK_NEXT) {
      if (v >= 0.5f && n > 0) {                // to the first IR of the previous / next pack (wraps around)
        int k = b.want >= 0 && b.want < n ? b.want : 0;
        const std::string here = b.lib[k].pack;
        if (i == P_IR_PACK_NEXT) {
          int j = k;
          do { j = (j + 1) % n; } while (j != k && b.lib[j].pack == here);
          k = j;
        } else {
          int start = k;
          while (start > 0 && b.lib[start - 1].pack == here) start--;
          int j = (start - 1 + n) % n;
          const std::string prev = b.lib[j].pack;
          if (prev == here) k = start;
          else { while (j > 0 && b.lib[j - 1].pack == prev) j--; k = j; }
        }
        b.want = k;
        b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false);
        browser_changed(w);
      }
      w->changed[i] = 1;
    } else if (i == P_IR_BROWSE) {
      browse = v >= 0.5f;
      w->changed[i] = 1;
    } else if (i == P_IR_DELETE) {
      if (v >= 0.5f && source_of(w) == 0) {      // the factory IRs are part of the plugin
        b.error = "Factory IRs can't be deleted";
      } else if (v >= 0.5f && b.want >= 0 && b.want < n) {
        const auto now = std::chrono::steady_clock::now();
        const std::string& path = b.lib[b.want].path;
        if (b.armed_path == path && now < b.armed_until) { b.armed_path.clear(); b.delete_req = true; kick(w, false); }
        else { b.armed_path = path; b.armed_until = now + std::chrono::seconds(kArmSeconds); }
      }
      w->changed[i] = 1;
    }
    w->update_display = 1;
  }
  if (browse) open_tones(w);
}

static float get_parameter(AEffect* e, int32_t i) {
  Inst* w = (Inst*)e->object;
  if (i < 0 || i >= P_COUNT) return 0;
  if (i == P_IR || i == P_IR_SRC) {
    std::lock_guard<std::mutex> lk(w->mu);
    return i == P_IR ? browser_norm(w->ir) : src_norm(w->ir);
  }
  if (i == P_PLAYHEAD) return w->eng ? w->eng->playhead() / (float)ReverbEngine::kPlay : 0.0f;
  if (i == P_SONICS) return 0;
  if (is_display(i)) return w->wave[i - P_WAVE0] / 23.0f;
  if (is_browser(i) || i == P_RESCAN || i == P_T3K_INFO) return 0;
  return w->norm[i];
}

static void notify(Inst* w) {                  // audio thread: report values the plugin changed itself
  {                                            // the playhead: at most every 9 blocks (~26 ms), hiding at once
    const int col = w->eng ? w->eng->playhead() : 0;
    w->playhead_age++;
    if (col != w->playhead_sent && (w->playhead_age >= 9 || col == 0)) {
      w->playhead_sent = col;
      w->playhead_age = 0;
      w->master(&w->fx, audioMasterAutomate, P_PLAYHEAD, 0, nullptr, col / (float)ReverbEngine::kPlay);
    }
  }
  for (int i = 0; i < P_COUNT; i++) {
    if (!w->changed[i].exchange(0)) continue;
    float v = 0;
    if (is_display(i) && i != P_PLAYHEAD && i != P_SONICS) v = w->wave[i - P_WAVE0] / (float)(8 * ReverbIR::kTones - 1);
    if (i == P_IR || i == P_IR_SRC) {
      std::unique_lock<std::mutex> lk(w->mu, std::try_to_lock);   // never wait on the audio thread
      if (!lk.owns_lock()) { w->changed[i] = 1; continue; }
      v = i == P_IR ? browser_norm(w->ir) : src_norm(w->ir);
    }
    w->master(&w->fx, audioMasterAutomate, i, 0, nullptr, v);
  }
  if (w->update_display.exchange(0)) w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, nullptr, 0);
}

static void process_replacing(AEffect* e, float** in, float** out, int32_t n) {
  Inst* w = (Inst*)e->object;
  if (w->master) notify(w);
  if (n <= 0) return;
  w->eng->process(in[0], in[1], out[0], out[1], n);
}

static void process_accumulate(AEffect* e, float** in, float** out, int32_t n) {   // legacy: adds to the outputs
  float l[256], r[256];
  for (int32_t off = 0; off < n; off += 256) {
    int32_t k = n - off < 256 ? n - off : 256;
    float* ip[2] = {in[0] + off, in[1] + off};
    float* op[2] = {l, r};
    process_replacing(e, ip, op, k);
    for (int32_t j = 0; j < k; j++) { out[0][off + j] += l[j]; out[1][off + j] += r[j]; }
  }
}

static void copy_text(void* dst, const std::string& s, size_t limit) {
  std::string t = s;
  if (t.size() > limit - 1) {
    size_t cut = limit - 4;
    while (cut > 0 && ((unsigned char)t[cut] & 0xC0) == 0x80) cut--;   // never split a UTF-8 character
    t = t.substr(0, cut) + "...";
  }
  memcpy(dst, t.c_str(), t.size() + 1);
}
static size_t text_limit(int i) {
  return (i == P_IR || i == P_IR_INFO || i == P_IR_PACK || i == P_T3K_INFO || i == P_SONICS) ? kTextLong : kText;
}
static std::string gear_name(const std::string& g) {
  if (g == "space") return "Space";
  if (g == "outboard") return "Outboard";
  if (g == "cab" || g == "ir") return "Cab";
  if (g == "experimental") return "Experimental";
  if (g == "pedal") return "Pedal";
  return g;
}

static std::string display_of(Inst* w, int i) {
  const param_t& p = PARAMS[i];
  char buf[96];
  if (i == P_T3K_INFO) {
    std::lock_guard<std::mutex> lk(w->mu);
    return w->t3k ? w->t3k->status() : "Tap BROWSE TONE3000";
  }
  if (is_browser(i)) {
    std::lock_guard<std::mutex> lk(w->mu);
    const Browser& b = w->ir;
    const int n = (int)b.lib.size();
    const LibEntry* cur = (b.want >= 0 && b.want < n) ? &b.lib[b.want] : nullptr;
    if (i == P_IR) {
      if (cur) {
        size_t s = cur->display.find_last_of('/');
        const std::string stem = s == std::string::npos ? cur->display : cur->display.substr(s + 1);
        return source_of(w) == 0 ? factory_name(stem) : stem;
      }
      if (!b.loaded.path.empty()) {              // playing an IR from the other list
        const std::string& f = b.loaded.file; size_t dot = f.find_last_of('.');
        const std::string stem = dot == std::string::npos ? f : f.substr(0, dot);
        return source_of(w) == 1 ? factory_name(stem) : stem;   // the other list: the factory when browsing My IRs
      }
      return b.saved.file.empty() ? "No IR" : b.saved.file;
    }
    if (i == P_IR_PACK) {
      if (!cur) return n == 0 ? "" : "-";
      if (cur->t3k && !cur->t3k_title.empty()) return cur->t3k_title;
      if (cur->pack.empty()) return "Loose files";
      size_t s = cur->pack.find_last_of('/');
      return s == std::string::npos ? cur->pack : cur->pack.substr(s + 1);
    }
    if (i == P_IR_SRC) return cur && cur->t3k ? "TONE3000" : "Local";
    if (i == P_IR_INFO) {
      const auto now = std::chrono::steady_clock::now();
      if (cur && b.armed_path == cur->path && now < b.armed_until) return "Tap DELETE again to delete it permanently";
      if (!b.notice.empty() && now < b.notice_until) return b.notice;
      if (!w->scanned || w->scanning) return "Searching...";
      if (b.loading) return "Loading...";
      if (!b.error.empty()) return b.error;
      if (n == 0) return source_of(w) == 0 ? "Factory IRs missing (reinstall)" : "No .wav IRs in a reverbs folder";
      if (!cur) return b.loaded.path.empty() ? "Init \xC2\xB7 pick a PRESET, or browse with \xE2\x97\x80 \xE2\x96\xB6"
                                             : "\xE2\x97\x80 \xE2\x96\xB6 browse this list";
      const long late = w->eng ? w->eng->misses() : 0;
      if (late > w->late_seen) {                 // a new late block: show it for 5 s (a one-off doesn't stick)
        w->late_seen = late;
        w->late_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      }
      if (late > 0 && std::chrono::steady_clock::now() < w->late_until) { snprintf(buf, sizeof buf, "Tail late %ld\xC3\x97 (CPU busy)", late); return buf; }
      const std::string ch = cur->path == b.loaded.path ? (b.loaded_stereo ? " \xC2\xB7 Stereo" : " \xC2\xB7 Mono") : "";
      if (cur->t3k) {
        std::string s = gear_name(cur->t3k_gear);
        s += (s.empty() ? "" : " \xC2\xB7 ") + std::string("IR");
        if (!cur->t3k_creator.empty()) s += " \xC2\xB7 @" + cur->t3k_creator;
        return s + ch;
      }
      int first = b.want, count = 0;
      while (first > 0 && b.lib[first - 1].pack == cur->pack) first--;
      for (int j = first; j < n && b.lib[j].pack == cur->pack; j++) count++;
      snprintf(buf, sizeof buf, "IR %d of %d \xC2\xB7 %s", b.want - first + 1, count, source_of(w) == 0 ? "Factory" : "Local");
      return std::string(buf) + ch;
    }
    return "";
  }
  if (i == P_PLAYHEAD) return p.opts[w->eng ? w->eng->playhead() : 0];
  if (i == P_SONICS) {
    std::lock_guard<std::mutex> lk(w->mu);
    return w->sonics.empty() ? (w->ir.loading ? "listening..." : "") : w->sonics;
  }
  if (is_display(i)) return p.opts[w->wave[i - P_WAVE0].load()];
  if (p.nopts > 1) return p.opts[(int)real_of(i, w->norm[i])];
  if (p.momentary) return "";
  const float v = real_of(i, w->norm[i]);
  if (i == P_HIGHCUT || (i == P_LOWCUT && v >= 1000)) snprintf(buf, sizeof buf, "%.1f kHz", v / 1000.0f);
  else if (!strcmp(p.unit, "Hz")) snprintf(buf, sizeof buf, "%.0f Hz", v);
  else if (!strcmp(p.unit, "ms")) snprintf(buf, sizeof buf, "%.0f ms", v);
  else if (!strcmp(p.unit, "%")) snprintf(buf, sizeof buf, "%.0f%%", v);
  else snprintf(buf, sizeof buf, "%.1f dB", v);
  return buf;
}

// state: "echo-vault=1\n", "<key>=<value>\n" per plain parameter, then the IR's path, relative path and file name
static int get_chunk(Inst* w) {
  std::string s = "echo-vault=1\n";
  char buf[64];
  for (int i = 0; i < P_COUNT; i++) {
    if (is_browser(i) || is_display(i) || PARAMS[i].momentary || i == P_T3K_INFO || PARAMS[i].popup_of >= 0) continue;
    snprintf(buf, sizeof buf, "%s=%.6g\n", PARAMS[i].key, real_of(i, w->norm[i]));
    s += buf;
  }
  std::lock_guard<std::mutex> lk(w->mu);
  const Browser& b = w->ir;
  const LibEntry& e = (b.want >= 0 && b.want < (int)b.lib.size()) ? b.lib[b.want] : b.saved;
  s += "ir_path=" + e.path + "\nir_display=" + e.display + "\nir_file=" + e.file + "\n";
  w->chunk = s;
  return (int)w->chunk.size() + 1;
}

static bool set_chunk(Inst* w, const char* data, size_t len) {
  std::string s(data, strnlen(data, len));
  // this plugin's state; also "ir-reverb=1" (its name before 1.0.0, in test projects)
  static const std::string kHeads[] = {"echo-vault=1\n", "ir-reverb=1\n"};
  size_t pos = std::string::npos;
  for (const std::string& h : kHeads)
    if (s.compare(0, h.size(), h) == 0) { pos = h.size(); break; }
  if (pos == std::string::npos) return false;
  LibEntry saved;
  while (pos < s.size()) {
    size_t nl = s.find('\n', pos);
    if (nl == std::string::npos) nl = s.size();
    std::string line = s.substr(pos, nl - pos);
    pos = nl + 1;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "ir_path") { saved.path = v; continue; }
    if (k == "ir_display") { saved.display = v; continue; }
    if (k == "ir_file") { saved.file = v; continue; }
    for (int i = 0; i < P_COUNT; i++) {
      if (is_browser(i) || PARAMS[i].momentary || k != PARAMS[i].key) continue;
      const param_t& p = PARAMS[i];
      float r = (float)atof(v.c_str());
      w->norm[i] = p.nopts > 1 ? clamp01(r / (p.nopts - 1)) : (p.max > p.min ? clamp01((r - p.min) / (p.max - p.min)) : 0);
      apply_param(w, i);
    }
  }
  std::lock_guard<std::mutex> lk(w->mu);
  w->ir.saved = saved;
  w->ir.lib = w->libs[source_of(w)];           // the project's list (Factory / My IRs)
  w->ir.error.clear();
  if (w->scanned) resolve(w->ir);
  requested(w->ir);
  kick(w, !w->scanned);
  for (int i = 0; i < P_COUNT; i++) w->changed[i] = 1;
  w->update_display = 1;
  return true;
}

static void rebuild_engine(Inst* w) {          // not while the host processes (effOpen / effSetSampleRate)
  std::lock_guard<std::mutex> lk(w->mu);
  w->eng.reset(new ReverbEngine(w->rate));
  for (int i = 0; i < P_COUNT; i++) if (!is_browser(i)) apply_param(w, i);
  w->ir.loaded = LibEntry();                   // the new engine has no IR: load it again
  w->ir.shape_valid = false;
  kick(w, false);
}

static intptr_t dispatcher(AEffect* e, int32_t op, int32_t idx, intptr_t v, void* p, float o) {
  Inst* w = (Inst*)e->object;
  switch (op) {
    case effOpen: return 1;
    case effClose: {
      {
        std::lock_guard<std::mutex> lk(w->mu);
        w->stop = true;
      }
      w->cv.notify_one();
      if (w->worker.joinable()) w->worker.join();      // 1. the worker (it reads w->t3k) has stopped
      std::unique_ptr<Tone3000> t;
      {
        std::lock_guard<std::mutex> lk(w->mu);
        t = std::move(w->t3k);
      }
      t.reset();                                       // 2. TONE3000's page and downloads (its callback locks mu)
      delete w;                                        // 3. (the engine joins its convolvers' threads)
      return 1;
    }
    case effGetPlugCategory: return kPlugCategEffect;
    case effGetEffectName:
    case effGetProductString: strncpy((char*)p, PLUG_NAME, 31); ((char*)p)[31] = 0; return 1;
    case effGetVendorString: strncpy((char*)p, PLUG_VENDOR, 31); ((char*)p)[31] = 0; return 1;
    case effGetVendorVersion: return PLUG_VERSION;
    case effGetVstVersion: return 2400;
    // the display parameters (waveform, glow) are read-only: not automatable (what MPC showed working)
    case effCanBeAutomated: return idx >= 0 && idx < P_COUNT && !is_display(idx) && idx != P_PRESET_OPEN;
    case effGetParamName:
      if (idx >= 0 && idx < P_COUNT) { strncpy((char*)p, PARAMS[idx].name, 31); ((char*)p)[31] = 0; }
      return 1;
    case effGetParamLabel: if (p) ((char*)p)[0] = 0; return 1;
    case effGetParamDisplay:
      if (idx < 0 || idx >= P_COUNT || !p) return 0;
      copy_text(p, display_of(w, idx), text_limit(idx));
      return 1;
    case effSetSampleRate:
      if (o > 1000 && std::fabs(o - w->rate) > 0.5) { w->rate = o; rebuild_engine(w); }
      return 1;
    case effSetBlockSize: return 1;            // any block size (multiples of 128 without added latency)
    case effMainsChanged: return 1;
    case effCanDo: return -1;
    case effGetChunk: { int len = get_chunk(w); *(void**)p = (void*)w->chunk.c_str(); return len; }
    case effSetChunk: return (v > 0 && p) ? (set_chunk(w, (const char*)p, (size_t)v) ? 1 : 0) : 0;
    default: return 0;
  }
}

extern "C" __attribute__((visibility("default"))) AEffect* VSTPluginMain(audioMasterCallback master) {
  Inst* w = new (std::nothrow) Inst();
  if (!w) return nullptr;
  w->master = master;
  for (int i = 0; i < P_COUNT; i++) { w->changed[i] = 0; w->norm[i] = PARAMS[i].def; }
  for (auto& c : w->wave) c = 0;
  AEffect* e = &w->fx;
  memset(e, 0, sizeof *e);
  e->magic = 0x56737450;
  e->dispatcher = dispatcher;
  e->process = process_accumulate;
  e->setParameter = set_parameter;
  e->getParameter = get_parameter;
  e->processReplacing = process_replacing;
  e->numParams = P_COUNT;
  e->numInputs = 2;
  e->numOutputs = 2;
  e->flags = effFlagsCanReplacing | effFlagsProgramChunks;
  e->uniqueID = PLUG_UID;
  e->version = PLUG_VERSION;
  e->object = w;
  w->eng.reset(new ReverbEngine(w->rate));
  for (int i = 0; i < P_COUNT; i++) if (!is_browser(i)) apply_param(w, i);
  w->worker = std::thread(worker_main, w);
  return e;
}
