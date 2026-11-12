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
#include <algorithm>
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
#include "presets.h"
#include "presets_web.h"
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
  effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effGetProgramName = 5, effGetProgramNameIndexed = 29,
  effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
  effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25,
  effCanBeAutomated = 26, effGetPlugCategory = 35, effGetEffectName = 45, effGetVendorString = 47,
  effGetProductString = 48, effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { audioMasterAutomate = 0, audioMasterGetTime = 7, audioMasterUpdateDisplay = 42, kVstTempoValid = 1 << 10 };
struct VstTimeInfo {                           // (audioMasterGetTime's answer; MPC gives a valid tempo: mpc-vst-plugins docs/NOTES.md)
  double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
  int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
};
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5 };
enum { kPlugCategEffect = 1 };
struct VstEvent { int32_t type, byteSize, deltaFrames, flags; char data[16]; };   // type 1: MIDI (VstMidiEvent)
struct VstEvents { int32_t numEvents; intptr_t reserved; VstEvent* events[2]; };
enum { kVstMidiType = 1 };

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
  P_MP_INFO,                                   // read-only: the My Presets status ("Saved: ...", "Tap DELETE again...")
  P_MP_SAVE, P_MP_DELETE,                      // SAVE the current sound (named from its IR); DELETE (two taps)
  P_MP_PHONE,                                  // NAME ON PHONE: opens the phone page where presets are named
  P_MENU,                                      // the list laid over the page: Closed / IR List / My Presets (read-only)
  P_IR_LIST, P_MP_LIST, P_MENU_CLOSE,          // open the IR list, open My Presets, close either
  P_MENU_PREV, P_MENU_NEXT,                    // the list's pages
  P_MENU_TITLE,                                // read-only: "IRs 7-12 of 83"
  P_ROW1,                                      // the list's rows (text = the item, value 1 = the one in use)
  P_ROW_LAST = P_ROW1 + 5,                     // (6 rows for IRs, 4 for presets)
  P_DECAY,                                     // Decay (experimental): the IR's decay time, band by band (IR shaping)
  P_LONG_IR,                                   // LONG IR: the high IR quality (engine/platform.h); a device setting, not a sound's
  P_LONG_IR_TAP,                               // its button: two taps turn it on (the first warns: CPU heavy), one turns it off
  P_LONG_IR_INFO,                              // read-only: "Off · IRs up to 5 s", "CPU heavy · tap again", "On · IRs up to 20 s"
  P_DUCK,                                      // Duck: the reverb turned down while the input plays (engine: ReverbParams::duck)
  P_PREDELAY_SYNC,                             // SYNC: Pre-delay in note values at the project tempo
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
  Quality loaded_quality;                      // and the quality it was prepared at (LONG IR)
  bool shape_valid = false;                    // false: nothing loaded (or the engine was rebuilt)
  std::string error;
  std::string armed_path;                      // DELETE: armed for this file until armed_until
  std::chrono::steady_clock::time_point armed_until, notice_until;
  bool delete_req = false;
  std::string notice;
};
static const int kArmSeconds = 4;
// A tap MPC delivers twice (or a toggle it echoes) within this is one tap: a two-tap DELETE must not arm and confirm
// on one touch. (LONG IR's warn-then-confirm taps behaved so on the device: the first tap turned it on.)
static const int kTapMs = 300;
static const double kDispGapMs = 100, kDispHoldMs = 600;   // UpdateDisplay pacing (notify())

// My Presets (engine/presets.h): the saved presets, the one selected, and what the worker is asked to do
struct MyPresets {
  std::vector<PresetData> list;                // sorted by name; filled by the worker
  int want = -1;                               // the selected one (an index into list)
  std::string sel_name;                        // its name: kept across a refresh, and saved in the project
  bool active = false;                         // a My Preset was the last thing applied (the PRESET field shows it)
  bool listed = false;                         // the folder has been read at least once
  bool refresh = true;                         // worker: read the presets folder
  bool save_req = false;                       // worker: save `pending` under a free name
  PresetData pending;                          // the sound to save (name = the base of its name)
  std::string delete_name;                     // worker: delete this preset
  std::string armed_name;                      // DELETE: armed for this preset until armed_until
  std::chrono::steady_clock::time_point armed_until, notice_until;
  std::string notice, error;
  bool show_addr = false;                      // NAME ON PHONE was tapped: the status line gives the phone address
};
// the parameters a preset keeps (the IR is kept apart)
static const struct { const char* key; int p; } kPresetParams[] = {
  {"mix", P_MIX}, {"predelay", P_PREDELAY}, {"lowcut", P_LOWCUT}, {"highcut", P_HIGHCUT}, {"width", P_WIDTH},
  {"length", P_LENGTH}, {"output", P_OUTPUT}, {"start", P_START}, {"fade_in", P_FADE_IN}, {"fade_out", P_FADE_OUT},
  {"stretch", P_STRETCH}, {"reverse", P_REVERSE}, {"feedback", P_FEEDBACK}, {"decay", P_DECAY}, {"duck", P_DUCK},
  {"predelay_sync", P_PREDELAY_SYNC}};

// SYNC: the Pre-delay knob's steps, in beats (quarter notes); T triplet, D dotted
static const struct { const char* name; double beats; } kNotes[] = {
  {"0", 0}, {"1/128", 1 / 32.0}, {"1/64", 1 / 16.0}, {"1/32", 1 / 8.0}, {"1/16 T", 1 / 6.0}, {"1/16", 1 / 4.0},
  {"1/16 D", 3 / 8.0}, {"1/8 T", 1 / 3.0}, {"1/8", 1 / 2.0}, {"1/8 D", 3 / 4.0}, {"1/4", 1.0}};
static const int kNoteCount = (int)(sizeof kNotes / sizeof kNotes[0]);

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
  MyPresets mp;                                // under mu
  std::string select_after_scan;               // a TONE3000 download to select once it's scanned
  std::unique_ptr<Tone3000> t3k;
  std::thread worker;
  std::atomic<int> changed[P_COUNT];
  std::atomic<int> update_display{0};
  std::atomic<float> host_view[P_COUNT];       // each value as the host last had it (it wrote it, or was told): -1 unknown
  std::chrono::steady_clock::time_point disp_last, disp_since;   // UpdateDisplay: last sent, first unsent request (audio thread)
  bool disp_wait = false;                      // an UpdateDisplay is due (audio thread)
  std::string chunk;
  std::vector<LibEntry> libs[2];               // 0: the factory IRs, 1: My IRs (reverbs/ folders)
  long late_seen = 0;                          // late tail blocks already noticed (host thread, under mu)
  std::chrono::steady_clock::time_point late_until;   // "Tail late" shows until then (5 s after the last one)
  std::atomic<int> wave[ReverbIR::kCols];      // the loaded IR's strands: level 0..7 + 8 x tint 0..2 (worker writes)
  std::atomic<int> menu{0};                    // the list laid over the page: 0 closed, 1 IR List, 2 My Presets
  int menu_page = 0;                           // its page (under mu)
  long marq_key = -1;                          // the list page the scrolling text belongs to (under mu)
  std::chrono::steady_clock::time_point marq_t0;   // when that page appeared: long names wait, then scroll
  int marq_sent = 0;                           // the scroll step the rows were last redrawn at (worker, under mu)
  std::string sonics;                          // the loaded IR's sonics, as words (under mu)
  int playhead_sent = 0, playhead_age = 0;     // the playhead as last reported (audio thread)
  std::chrono::steady_clock::time_point long_tap_at;   // LONG IR's old tap parameter: the last tap taken (see kTapMs)
  std::vector<LibEntry> prog_irs;              // MPC's PRESET menu: My IRs as programs (the folder index, read at load)
  std::atomic<int> cur_prog{0};                // the program MPC's menu shows as current
  std::chrono::steady_clock::time_point chunk_at;   // a project's state arrived (a program picked just after: the host)
  std::atomic<bool> preset_clean{false};       // the settings are a factory preset's, untouched: a My IR picked -> Init
  std::atomic<bool> init_pending{false};       // (a TONE3000 download selected on the worker: Init after publishing)
  std::atomic<double> bpm{120.0};              // SYNC: the host's tempo (audio thread writes; 120 until it gives one)
  std::atomic<bool> tempo_ok{false};           // the host gave a tempo
};

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// Low Cut and High Cut turn on a log scale (each octave the same travel; linear, 20-200 Hz was the first 18% of Low
// Cut's turn). Projects and presets store real values, so they come back the same.
static bool is_log(int i) { return i == P_LOWCUT || i == P_HIGHCUT; }
static float real_of(int i, float n) {
  const param_t& p = PARAMS[i];
  if (p.nopts > 1) return (float)lroundf(clamp01(n) * (p.nopts - 1));
  if (is_log(i)) return p.min * std::pow(p.max / p.min, clamp01(n));
  return p.min + (p.max - p.min) * clamp01(n);
}
static float norm_of(int i, float real) {        // real_of's inverse
  const param_t& p = PARAMS[i];
  if (p.nopts > 1) return clamp01(real / (p.nopts - 1));
  if (!(p.max > p.min)) return 0;
  if (is_log(i)) return clamp01(std::log(std::max(real, p.min) / p.min) / std::log(p.max / p.min));
  return clamp01((real - p.min) / (p.max - p.min));
}
static float def_norm(int i) {                   // (params.h's defaults are on a linear scale)
  const param_t& p = PARAMS[i];
  return is_log(i) ? norm_of(i, p.min + (p.max - p.min) * p.def) : p.def;
}
// factory presets: each loads factory/<category>/<name>.wav with these settings (real values, as the page shows)
struct Preset {
  const char *name, *category;
  int mix, predelay, lowcut, highcut, width, length, output, start, fade_in, fade_out, stretch, reverse, feedback;
};
// Feedback (0-100%, eased, damped and guarded in the engine): these keep the tails the presets had before the runaway
// fix (Tape Corridor 20 -> 34, Dream Ping-Pong 15 -> 39, Intercom Echo 25 -> 43, Shimmer Fog 10 -> 12: its repeats are
// now damped, 4.1 s for 4.4 s). Liminal Hz's 12 ran away (an endless drone 9 dB over the rest): 50 is a long tail that ends.
// Levels (an audit of every preset, 1.4.0): over drums, a vocal, a synth chord and pink noise each preset's peak stays
// within ~3 dB of the input's and its level within about +-3 dB of the dry; a -12 dBFS sine sweep stays under 0 dBFS;
// summed to mono a vocal loses at most ~4 dB. Resonant IRs (Sub Tunnel, Liminal Hz's feedback on held notes, the
// Textures; unit-energy IRs put much more gain on their peaks than a room) get less Mix, a trimmed Output, or less Width.
static const Preset kPresets[] = {
  {"Empty Mall", "Spaces", 35, 20, 90, 9000, 120, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Pool Rooms", "Spaces", 30, 8, 120, 14000, 110, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Fluorescent Hall", "Spaces", 40, 30, 60, 8000, 130, 100, 0, 0, 0, 12, 100, 0, 0},
  {"Backrooms", "Spaces", 28, 4, 80, 6000, 90, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Stairwell Flutter", "Echoes", 30, 0, 150, 9000, 110, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Tape Corridor", "Echoes", 32, 0, 120, 7000, 120, 100, 0, 0, 0, 8, 100, 0, 34},
  {"Dream Ping-Pong", "Echoes", 35, 0, 140, 12000, 160, 100, 0, 0, 0, 8, 100, 0, 39},
  {"Parking Slap", "Echoes", 30, 0, 100, 8000, 120, 100, 0, 0, 0, 10, 100, 0, 0},
  {"Intercom Echo", "Echoes", 30, 0, 300, 4000, 100, 100, 0, 0, 0, 10, 100, 0, 43},
  {"Glass Void", "Strange", 30, 40, 200, 14000, 115, 100, 0, 0, 5, 15, 100, 0, 0},
  {"Reverse Bloom", "Strange", 40, 0, 120, 10000, 140, 100, 0, 0, 0, 4, 100, 0, 0},
  {"Sub Tunnel", "Strange", 22, 20, 30, 5000, 120, 100, -3, 0, 0, 12, 100, 0, 0},
  {"Shimmer Fog", "Strange", 38, 50, 250, 16000, 170, 100, 0, 0, 10, 15, 100, 0, 12},
  {"Liminal Hz", "Strange", 30, 35, 80, 11000, 150, 100, -2, 0, 5, 15, 100, 0, 45},
  // Textures: not rooms. The input played through an object, a cloud of grains, a chord, a breath, a bounce
  {"Struck Pipe", "Textures", 20, 0, 120, 12000, 130, 100, -2, 0, 0, 10, 100, 0, 0},
  {"Grain Cloud", "Textures", 40, 10, 150, 14000, 120, 100, -4, 0, 0, 15, 100, 0, 0},
  {"Tuned Chord", "Textures", 12, 0, 100, 10000, 100, 100, -2, 0, 0, 10, 100, 0, 0},
  {"Breath", "Textures", 35, 0, 200, 12000, 110, 100, -5, 0, 0, 15, 100, 0, 0},
  {"Bouncing Ball", "Textures", 35, 0, 120, 12000, 150, 100, 0, 0, 0, 10, 100, 0, 0}
};
static const int kPresetCount = (int)(sizeof kPresets / sizeof kPresets[0]);

// What each factory IR suits best: shown after its category on the page's pack line ("Strange · best on pads, vocals").
static const struct { const char *ir, *best; } kBestOn[] = {
  {"Empty Mall", "drums, vocals"}, {"Pool Rooms", "snares, vocals"}, {"Fluorescent Hall", "keys, pads"},
  {"Backrooms", "pads, drones"}, {"Stairwell Flutter", "snares, claps"}, {"Tape Corridor", "vocals, keys"},
  {"Dream Ping-Pong", "keys, plucks"}, {"Parking Slap", "drums, rap vocals"}, {"Intercom Echo", "vocals, FX"},
  {"Glass Void", "pads, vocals"}, {"Reverse Bloom", "drums, vocals"}, {"Sub Tunnel", "drums, drones"},
  {"Shimmer Fog", "pads, guitars"}, {"Liminal Hz", "chords, pads"}, {"Struck Pipe", "drums, percussion"},
  {"Grain Cloud", "vocals, pads"}, {"Tuned Chord", "drums, vocals"}, {"Breath", "vocals, hi-hats"},
  {"Bouncing Ball", "snares, one-shots"}};
static const char* best_on(const std::string& ir) {
  for (const auto& b : kBestOn) if (ir == b.ir) return b.best;
  return nullptr;
}

// VST programs: MPC's own PRESET menu (top of the screen) lists them and loads one with effSetProgram
// (mpc-vst-plugins docs/NOTES.md, 2026-10-07). VST2 has no folders, so the list is grouped by what each preset is for,
// named "Use: Name" (<= 23 characters): Init, the Drum, Vocal, Keys, Pad and FX presets, then My IRs ("My IR: Name",
// each with Init's settings), read from the folder index when the plugin loads, so IRs added or deleted show on the
// next load. The factory programs keep their places; My IRs come last.
static const struct { const char *use, *name; } kProgOrder[] = {
  {"Drum", "Empty Mall"}, {"Drum", "Pool Rooms"}, {"Drum", "Stairwell Flutter"}, {"Drum", "Parking Slap"},
  {"Drum", "Struck Pipe"}, {"Drum", "Bouncing Ball"},
  {"Vocal", "Tape Corridor"}, {"Vocal", "Intercom Echo"}, {"Vocal", "Glass Void"},
  {"Keys", "Fluorescent Hall"}, {"Keys", "Dream Ping-Pong"}, {"Keys", "Liminal Hz"},
  {"Pad", "Backrooms"}, {"Pad", "Shimmer Fog"},
  {"FX", "Reverse Bloom"}, {"FX", "Sub Tunnel"}, {"FX", "Grain Cloud"}, {"FX", "Tuned Chord"}, {"FX", "Breath"}};
static_assert(sizeof kProgOrder / sizeof kProgOrder[0] == sizeof kPresets / sizeof kPresets[0], "every preset in the menu");
static const int kProgramCount = 1 + kPresetCount;   // Init and the factory presets (My IRs follow)
static const int kMaxProgIrs = 500;                  // (a very big IR collection: the first 500)
static int preset_of_prog(int p) {                   // program -> PRESET index (0 Init, 1.. kPresets); -1: a My IR
  if (p <= 0) return 0;
  if (p >= kProgramCount) return -1;
  for (int k = 0; k < kPresetCount; k++) if (!strcmp(kPresets[k].name, kProgOrder[p - 1].name)) return k + 1;
  return 0;
}
static int prog_of_preset(int k) {                   // PRESET index -> program
  if (k <= 0 || k > kPresetCount) return 0;
  for (int p = 0; p < kPresetCount; p++) if (!strcmp(kPresets[k - 1].name, kProgOrder[p].name)) return p + 1;
  return 0;
}
static std::string fit23(std::string t) {            // VST2 program names: 23 bytes, never cutting a UTF-8 letter
  if (t.size() <= 23) return t;
  size_t n = 23;
  while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--;
  return t.substr(0, n);
}
static int program_count(const Inst* w) { return kProgramCount + (int)w->prog_irs.size(); }
static std::string program_name(const Inst* w, int k) {
  if (k == 0) return "Init";
  if (k > 0 && k < kProgramCount) return std::string(kProgOrder[k - 1].use) + ": " + kProgOrder[k - 1].name;
  const int j = k - kProgramCount;
  if (j < 0 || j >= (int)w->prog_irs.size()) return "";
  std::string f = w->prog_irs[j].file;
  const size_t d = f.find_last_of('.');
  return fit23("My IR: " + (d == std::string::npos ? f : f.substr(0, d)));
}
static void ir_picked(Inst* w);                // (below: an IR picked; maybe back to Init)
static int prog_of_ir(const Inst* w, const std::string& path) {   // a My IR's program (-1: not in the menu)
  for (int j = 0; j < (int)w->prog_irs.size(); j++) if (!path.empty() && w->prog_irs[j].path == path) return kProgramCount + j;
  return -1;
}

static bool is_display(int i) { return (i >= P_WAVE0 && i <= P_PLAYHEAD) || i == P_SONICS; }   // read-only, not automatable
static bool is_browser(int i) { return (i >= P_IR && i <= P_IR_BROWSE) || i == P_IR_DELETE; }
static bool is_mp(int i) { return i >= P_MP_INFO && i <= P_MP_PHONE; }   // My Presets controls
static bool is_menu(int i) { return i >= P_MENU && i <= P_ROW_LAST; }     // the list laid over the page (state, buttons, rows)
static bool is_row(int i) { return i >= P_ROW1 && i <= P_ROW_LAST; }
static bool is_long(int i) { return i >= P_LONG_IR && i <= P_LONG_IR_INFO; }   // LONG IR: the setting, its button, its line
static bool long_ir_on(const Inst* w) { return platform::kGen2 || w->norm[P_LONG_IR] > 0.5f; }
static Quality quality_of(const Inst* w) { return platform::quality(long_ir_on(w)); }

static bool sync_on(const Inst* w) { return w->norm[P_PREDELAY_SYNC] > 0.5f; }
static int sync_note(const Inst* w) { return (int)lroundf(clamp01(w->norm[P_PREDELAY]) * (kNoteCount - 1)); }
static void sync_predelay(Inst* w) {           // SYNC: the note at the tempo, as ms (any thread; the engine glides to it)
  const double ms = kNotes[sync_note(w)].beats * 60000.0 / std::max(20.0, w->bpm.load());
  w->eng->params.predelay_ms = (float)std::min(ms, (double)ReverbEngine::kMaxPredelay);
}
static void apply_param(Inst* w, int i) {      // plain parameter -> engine (atomics)
  ReverbParams& e = w->eng->params;
  const float v = real_of(i, w->norm[i]);
  switch (i) {
    case P_MIX: e.mix = v / 100.0f; break;
    case P_PREDELAY: if (!sync_on(w)) e.predelay_ms = v; else sync_predelay(w); break;
    case P_LOWCUT: e.lowcut_hz = v; break;
    case P_HIGHCUT: e.highcut_hz = v; break;
    case P_WIDTH: e.width = v / 100.0f; break;
    case P_OUTPUT: e.output_db = v; break;
    case P_FEEDBACK: e.feedback = v / 100.0f; break;
    case P_DUCK: e.duck = v / 100.0f; break;
    case P_PREDELAY_SYNC: apply_param(w, P_PREDELAY); break;   // (synced, process() sets the time from the tempo)
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

// ---- the list laid over the page: IR List (menu 1, 6 rows a page) and My Presets (menu 2, 4 rows) ----------------------
// All of these hold mu. The page draws the rows as live text; a tap on a row is a parameter write.
static int menu_page_size(int m) { return m == 1 ? 6 : m == 2 ? 4 : 0; }
static int menu_count(const Inst* w, int m) { return m == 1 ? (int)w->ir.lib.size() : m == 2 ? (int)w->mp.list.size() : 0; }
static int menu_selected(const Inst* w, int m) { return m == 1 ? w->ir.want : m == 2 ? w->mp.want : -1; }
static int menu_pages(const Inst* w, int m) {
  const int sz = menu_page_size(m);
  return sz ? std::max(1, (menu_count(w, m) + sz - 1) / sz) : 1;
}
static void menu_clamp(Inst* w) { w->menu_page = std::max(0, std::min(w->menu_page, menu_pages(w, w->menu) - 1)); }
static void menu_goto_selected(Inst* w) {        // the page that holds the item in use
  const int sel = menu_selected(w, w->menu), sz = menu_page_size(w->menu);
  w->menu_page = sel >= 0 && sz ? sel / sz : 0;
  menu_clamp(w);
}

// factory IR files have no spaces ("Dream_Ping-Pong.wav", the release tool's rule); the page shows the name
static bool ieq(const std::string& a, const std::string& b) {   // equal ignoring ASCII case (a card may be FAT)
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}
static std::string factory_name(std::string s) { for (char& c : s) if (c == '_') c = ' '; return s; }

// ---- long names scroll: the page shows a window of the text, and it slides until the end shows, then stays ----------
// The live text can't be measured, so widths are estimated per character (Titillium Web at the row's 30 px): the window
// is what fits in kMarqPx. A name that fits never moves.
static const int kMarqPx = 470;
static int glyph_px(unsigned char c) {
  if (c >= 0x80) return 13;
  if (strchr("il.,:;|!' ", c) || c == 'I' || c == 'j') return c == ' ' ? 6 : 7;
  if (strchr("ftr()[]-/", c)) return 9;
  if (c == 'm' || c == 'w') return 20;
  if (c == 'M' || c == 'W') return 25;
  if (c >= 'A' && c <= 'Z') return 16;
  return 13;
}
static void marq_cells(const std::string& s, std::vector<size_t>& off, std::vector<int>& px) {   // per UTF-8 character
  off.clear(); px.clear();
  for (size_t i = 0; i < s.size(); i++) {
    const unsigned char c = (unsigned char)s[i];
    if ((c & 0xC0) == 0x80) continue;
    off.push_back(i);
    px.push_back(c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xB7 ? 7 : glyph_px(c));   // the "\xC2\xB7" dot
  }
}
static int marq_max_step(const std::string& s) {     // characters to slide so that the end shows (0: it all fits)
  std::vector<size_t> off; std::vector<int> px;
  marq_cells(s, off, px);
  int tot = 0; for (int v : px) tot += v;
  int k = 0;
  while (tot > kMarqPx && k < (int)px.size()) tot -= px[k++];
  return k;
}
static std::string marq_window(const std::string& s, int step) {
  const int mx = marq_max_step(s);
  if (mx == 0) return s;
  std::vector<size_t> off; std::vector<int> px;
  marq_cells(s, off, px);
  const size_t k = (size_t)std::min(step, mx);
  return s.substr(off[k]);                           // (at the end of the slide the rest fits; before it, the box clips)
}
// the step the scrolling is at: 0 for 1.5 s after a page appears, then one character every 250 ms (holds mu)
static int marq_step_now(Inst* w) {
  const long key = (long)w->menu * 1000000L + (long)w->menu_page * 1000L + (long)(w->menu == 1 ? w->ir.lib.size() : w->mp.list.size());
  const auto now = std::chrono::steady_clock::now();
  if (key != w->marq_key) { w->marq_key = key; w->marq_t0 = now; w->marq_sent = 0; }
  const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(now - w->marq_t0).count();
  return ms < 1500 ? 0 : (int)((ms - 1500) / 250) + 1;
}

static std::string menu_row_text(Inst* w, int m, int idx) {   // a row's whole text (holds mu)
  if (m == 2) return w->mp.list[idx].name;
  const LibEntry& e = w->ir.lib[idx];            // "Pack \xC2\xB7 name"
  const size_t sl = e.display.find_last_of('/');
  std::string stem = sl == std::string::npos ? e.display : e.display.substr(sl + 1);
  if (source_of(w) == 0) stem = factory_name(stem);
  std::string pack = e.t3k && !e.t3k_title.empty() ? e.t3k_title : e.pack;
  const size_t ps = pack.find_last_of('/');
  if (ps != std::string::npos) pack = pack.substr(ps + 1);
  return pack.empty() ? stem : pack + " \xC2\xB7 " + stem;
}
// the worker's tick: while a page has a long name still sliding, the rows are drawn again at each new step (holds mu)
static void marq_tick(Inst* w) {
  const int m = w->menu;
  if (!m) return;
  const int step = marq_step_now(w), sz = menu_page_size(m), n = menu_count(w, m);
  int mx = 0;
  for (int r = 0; r < sz; r++) {
    const int idx = w->menu_page * sz + r;
    if (idx < n) mx = std::max(mx, marq_max_step(menu_row_text(w, m, idx)));
  }
  const int shown = std::min(step, mx);
  if (shown != w->marq_sent) { w->marq_sent = shown; w->update_display = 1; }
}
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
  s.decay = real_of(P_DECAY, w->norm[P_DECAY]) / 100.0;
  return s;
}
static bool is_shape(int i) {
  return i == P_LENGTH || i == P_START || i == P_FADE_IN || i == P_FADE_OUT || i == P_STRETCH || i == P_REVERSE || i == P_DECAY;
}

static void load_ir(Inst* w) {
  std::unique_lock<std::mutex> lk(w->mu);
  Browser& b = w->ir;
  const IRShape shape = shape_of(w);
  const Quality q = quality_of(w);
  if (b.want < 0 || b.want >= (int)b.lib.size()) { b.loading = false; return; }
  const LibEntry e = b.lib[b.want];
  if (e.path == b.loaded.path && b.shape_valid && shape == b.loaded_shape && q == b.loaded_quality) { b.loading = false; return; }
  b.loading = true;
  const double rate = w->rate;
  w->update_display = 1;
  lk.unlock();
  std::string err;
  std::unique_ptr<ReverbIR> r = ReverbIR::load(e.path, rate, shape, &err, q);
  lk.lock();
  b.loading = false;
  if (!w->eng || w->rate != rate) return;
  if (!r) {
    b.error = err.size() < 32 && !err.empty() ? err : "Can't load " + e.file;
    b.loaded = LibEntry();
    b.loaded.path = e.path;                    // don't retry a broken file in a loop
    b.loaded_shape = shape;
    b.loaded_quality = q;
    b.shape_valid = true;
    w->eng->set_ir(nullptr);
    for (int c = 0; c < ReverbIR::kCols; c++) { w->wave[c] = 0; w->changed[P_WAVE0 + c] = 1; }
    w->sonics.clear();
  } else {
    b.error.clear();
    b.loaded = e;
    b.loaded_shape = shape;
    b.loaded_quality = q;
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
      const char* fenv = getenv("MPCNAM_FACTORY");
      const auto factory = Library::scan_folder(fenv ? fenv : Library::plugin_dir() + "/factory", ".wav");
      // Publish a scan's result: the lists, the saved or loaded IR found again, a TONE3000 download selected.
      auto publish = [&](const std::vector<LibEntry>& mine) {
        std::lock_guard<std::mutex> lk(w->mu);
        w->libs[0] = factory;
        w->libs[1] = mine;
        w->ir.lib = w->libs[source_of(w)];
        resolve(w->ir);
        if (!w->select_after_scan.empty()) {     // a TONE3000 download (in My IRs): show that list and select it
          w->norm[P_SOURCE] = 1.0f;
          w->changed[P_SOURCE] = 1;
          w->ir.lib = w->libs[1];
          int k = Library::find(w->ir.lib, w->select_after_scan, "", "");
          if (k >= 0) { w->ir.want = k; w->ir.saved = LibEntry(); w->ir.error.clear(); w->init_pending = true; }
          w->select_after_scan.clear();
        }
        requested(w->ir);
        w->scanned = true;
        w->scanning = false;
        menu_clamp(w);
        browser_changed(w);
        w->update_display = 1;
      };
      // 1. Quick: the reverbs/ folders found last time (plus the plugin's own), no walk of the cards. If it already
      //    holds what is waited for (the saved IR, a fresh TONE3000 download) the browser is usable at once; if not,
      //    "Searching..." stays until the walk answers.
      std::vector<LibEntry> quick, mine;
      bool published_quick = false;
      if (Library::scan_quick("reverbs", ".wav", &quick)) {
        bool complete = true;
        {
          std::lock_guard<std::mutex> lk(w->mu);
          const LibEntry& s = !w->ir.saved.path.empty() || !w->ir.saved.file.empty() ? w->ir.saved : w->ir.loaded;
          if (source_of(w) == 1 && !(s.path.empty() && s.file.empty()) &&
              Library::find(quick, s.path, s.display, s.file) < 0) complete = false;
          if (!w->select_after_scan.empty() && Library::find(quick, w->select_after_scan, "", "") < 0) complete = false;
        }
        if (complete) { publish(quick); published_quick = true; if (w->init_pending.exchange(false)) ir_picked(w); load_ir(w); }   // the saved IR sounds before the walk ends
      }
      // 2. The walk of the cards (it also refreshes the folder index for next time). Publishes again only if it
      //    found something different from the quick pass, so a list being browsed doesn't jump for nothing.
      mine = Library::scan("reverbs", ".wav");
      bool same = published_quick && mine.size() == quick.size();
      for (size_t i = 0; same && i < mine.size(); i++) same = mine[i].path == quick[i].path;
      if (!same) publish(mine);
      if (w->init_pending.exchange(false)) ir_picked(w);   // (a TONE3000 download: one of your own IRs, picked)
    }
    {                                          // My Presets: read the folder, save the asked-for sound, delete
      bool refresh, save;
      PresetData pend;
      std::string del;
      {
        std::lock_guard<std::mutex> lk(w->mu);
        MyPresets& m = w->mp;
        refresh = m.refresh; save = m.save_req; del = m.delete_name; pend = m.pending;
        m.refresh = m.save_req = false;
        m.delete_name.clear();
      }
      if (refresh || save || !del.empty()) {
        std::string err, notice, select;
        if (!del.empty()) { if (Presets::remove(del, &err)) { notice = "Deleted: " + del; err.clear(); } }
        if (save) {
          const std::string name = Presets::unique_name(pend.name);   // named from its IR, numbered if taken
          pend.name = name;
          if (Presets::save(pend, false, &err)) { notice = "Saved: " + name; select = name; err.clear(); }
        }
        std::vector<PresetData> l = Presets::list();
        std::lock_guard<std::mutex> lk(w->mu);
        MyPresets& m = w->mp;
        m.list = std::move(l);
        m.listed = true;
        if (!select.empty()) { m.sel_name = select; m.active = true; }   // a new preset is the selected one
        if (!del.empty() && ieq(m.sel_name, del)) { m.sel_name.clear(); m.active = false; }
        m.want = -1;
        for (int k = 0; k < (int)m.list.size(); k++) if (!m.sel_name.empty() && ieq(m.list[k].name, m.sel_name)) m.want = k;
        if (!notice.empty()) { m.notice = notice; m.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(4); m.error.clear(); }
        else if (!err.empty()) m.error = err;
        if (!select.empty() && w->menu == 2 && m.want >= 0) w->menu_page = m.want / menu_page_size(2);   // show it
        menu_clamp(w);
        w->changed[P_PRESET] = 1;              // (the PRESET field names a My Preset)
        w->update_display = 1;
      }
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
        if (ok) {                              // taken out of the lists here: no walk of the cards ("Searching...")
          Browser& b = w->ir;
          b.notice = "Deleted";
          b.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
          b.error.clear();
          b.loaded = LibEntry();
          auto drop = [&](std::vector<LibEntry>& l) {
            l.erase(std::remove_if(l.begin(), l.end(), [&](const LibEntry& x) { return x.path == victim.path; }), l.end());
          };
          drop(w->libs[1]);
          b.lib = w->libs[source_of(w)];
          if (b.want >= (int)b.lib.size()) b.want = b.lib.empty() ? -1 : 0;   // the next IR, as a rescan picked
          if (b.want < 0) {                    // the last one: nothing plays
            b.shape_valid = false;
            if (w->eng) w->eng->set_ir(nullptr);
            for (int c = 0; c < ReverbIR::kCols; c++) { w->wave[c] = 0; w->changed[P_WAVE0 + c] = 1; }
            w->sonics.clear();
          }
          requested(b);
          menu_clamp(w);
          browser_changed(w);
        } else {
          w->ir.error = err;
        }
        w->update_display = 1;
      }
      std::lock_guard<std::mutex> lk(w->mu);   // an armed DELETE or a notice ran out: refresh the info line
      const auto now = std::chrono::steady_clock::now();
      marq_tick(w);                            // (long names in an open list slide)
      if (!w->ir.armed_path.empty() && now >= w->ir.armed_until) { w->ir.armed_path.clear(); w->update_display = 1; }
      if (!w->ir.notice.empty() && now >= w->ir.notice_until) { w->ir.notice.clear(); w->update_display = 1; }
      if (!w->mp.armed_name.empty() && now >= w->mp.armed_until) { w->mp.armed_name.clear(); w->update_display = 1; }
      if (!w->mp.notice.empty() && now >= w->mp.notice_until) { w->mp.notice.clear(); w->update_display = 1; }
    }
    load_ir(w);
    std::lock_guard<std::mutex> lk(w->mu);
    if (w->eng) w->eng->collect();
  }
}

// ---- the phone web server (TONE3000, and the My Presets page) ------------------------------------------------------
static Tone3000* ensure_t3k(Inst* w);          // below, after the My Presets hooks it serves
static void open_tones(Inst* w) {
  ensure_t3k(w)->start("space");
  w->update_display = 1;
}

// ---- VST callbacks -------------------------------------------------------------------------------------------------
static void set_real(Inst* w, int i, float real) {   // a plain parameter by its real value (presets)
  const param_t& p = PARAMS[i];
  w->norm[i] = norm_of(i, real);
  apply_param(w, i);
  w->changed[i] = 1;
}

// a sound setting (what Init resets and a factory preset sets): not the IR, the lists, the buttons, LONG IR
static bool is_setting(int i) {
  return !is_browser(i) && !is_display(i) && !is_mp(i) && !PARAMS[i].momentary && i != P_PRESET && i != P_PRESET_OPEN &&
         i != P_RESCAN && i != P_T3K_INFO && i != P_SOURCE && !is_menu(i) && !is_long(i);
}
static void reset_settings(Inst* w) {          // every setting at its default (Init's), the IR as it is
  for (int i = 0; i < P_COUNT; i++)
    if (is_setting(i)) { w->norm[i] = def_norm(i); apply_param(w, i); w->changed[i] = 1; }
}
// One of your own IRs picked while the settings are still a factory preset's, untouched: they belong to that preset's
// IR, so they go back to Init (as the My IR programs in MPC's menu do). Settings you changed stay. Not under mu.
static void back_to_init(Inst* w) {
  if (!w->preset_clean.exchange(false)) return;
  reset_settings(w);
  w->norm[P_PRESET] = 0;
  w->changed[P_PRESET] = 1;
  std::lock_guard<std::mutex> lk(w->mu);
  if (w->ir.want >= 0) w->ir.loading = true;  // (Init's shape: the IR is prepared again)
  w->cur_prog = std::max(0, prog_of_ir(w, w->ir.want >= 0 && w->ir.want < (int)w->ir.lib.size() ? w->ir.lib[w->ir.want].path : ""));
  kick(w, false);
  w->update_display = 1;
}

// an IR picked on the page (arrows, packs, the IR list, a TONE3000 download): MPC's menu shows its program; one of
// your own after an untouched factory preset: back to Init. Not under mu.
static void ir_picked(Inst* w) {
  int prog = -1; bool mine;
  {
    std::lock_guard<std::mutex> lk(w->mu);
    mine = source_of(w) == 1;
    const Browser& b = w->ir;
    if (mine && b.want >= 0 && b.want < (int)b.lib.size()) prog = prog_of_ir(w, b.lib[b.want].path);
  }
  if (!mine) return;
  back_to_init(w);
  if (prog >= 0) w->cur_prog = prog;
}

// PRESET picked (k = 0: Init): settings, then the factory IR (or none); closes the popup
static void apply_preset(Inst* w, int k) {
  if (k < 0 || k > kPresetCount) return;
  w->norm[P_PRESET] = (float)k / kPresetCount;
  w->norm[P_PRESET_OPEN] = 0;                  // a pick closes the list
  w->changed[P_PRESET] = 1;
  w->changed[P_PRESET_OPEN] = 1;
  w->cur_prog = prog_of_preset(k);
  w->preset_clean = k > 0;
  if (k == 0) {                                // Init: every setting at its default, no IR
    reset_settings(w);
    std::lock_guard<std::mutex> lk(w->mu);
    w->mp.active = false;
    w->mp.notice.clear(); w->mp.armed_name.clear();
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
                         {P_REVERSE, p.reverse}, {P_FEEDBACK, p.feedback}, {P_DECAY, 100}, {P_DUCK, 0},
                         {P_PREDELAY_SYNC, 0}};
  for (const auto& kv : vals) set_real(w, kv[0], (float)kv[1]);
  set_real(w, P_SOURCE, 0);                    // the factory list
  std::lock_guard<std::mutex> lk(w->mu);
  w->mp.active = false;
  w->mp.notice.clear(); w->mp.armed_name.clear();
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

// a My IR from MPC's PRESET menu: Init's settings with that IR (from My IRs; found once they're scanned if not yet)
static void pick_ir_program(Inst* w, int j) {
  if (j < 0 || j >= (int)w->prog_irs.size()) return;
  const LibEntry e = w->prog_irs[j];
  reset_settings(w);
  w->preset_clean = false;
  w->norm[P_PRESET] = 0; w->changed[P_PRESET] = 1;
  w->norm[P_PRESET_OPEN] = 0; w->changed[P_PRESET_OPEN] = 1;
  set_real(w, P_SOURCE, 1);                    // My IRs
  std::lock_guard<std::mutex> lk(w->mu);
  w->mp.active = false;
  w->mp.notice.clear(); w->mp.armed_name.clear();
  Browser& b = w->ir;
  b.lib = w->libs[1];
  b.want = Library::find(b.lib, e.path, e.display, e.file);
  b.saved = b.want < 0 ? e : LibEntry();
  b.error.clear();
  if (b.want < 0 && w->scanned) b.error = "IR missing: " + e.display;   // (deleted since the plugin loaded)
  requested(b);
  kick(w, false);
  browser_changed(w);
  w->update_display = 1;
}

// ---- My Presets ----------------------------------------------------------------------------------------------------
static std::string stem_of(const std::string& f) { const size_t d = f.find_last_of('.'); return d == std::string::npos ? f : f.substr(0, d); }
// the sound as it is now (holds mu): the settings a preset keeps and the IR in use; name = what to call it
static PresetData mp_snapshot(Inst* w) {
  PresetData p;
  for (const auto& kp : kPresetParams) p.values.emplace_back(kp.key, real_of(kp.p, w->norm[kp.p]));
  const Browser& b = w->ir;
  const LibEntry* e = !b.loaded.path.empty() ? &b.loaded
                      : (b.want >= 0 && b.want < (int)b.lib.size() ? &b.lib[b.want] : nullptr);
  p.name = "My Preset";
  if (e) {
    p.ir_src = Library::find(w->libs[0], e->path, "", "") >= 0 ? 0 : 1;
    p.ir_path = e->path; p.ir_display = e->display; p.ir_file = e->file;
    p.name = p.ir_src == 0 ? factory_name(stem_of(e->file)) : stem_of(e->file);
  }
  return p;
}

// a preset's settings, then its IR (found in its list, or the other; waits for the first scan if need be)
static void mp_apply(Inst* w, const PresetData& p) {
  set_real(w, P_DECAY, 100);                   // (presets saved before Decay: as recorded)
  set_real(w, P_DUCK, 0);                      // (and before Duck and SYNC: off)
  set_real(w, P_PREDELAY_SYNC, 0);
  for (const auto& kv : p.values)
    for (const auto& kp : kPresetParams)
      if (kv.first == kp.key) set_real(w, kp.p, kv.second);
  std::lock_guard<std::mutex> lk(w->mu);
  Browser& b = w->ir;
  if (!p.ir_path.empty() || !p.ir_file.empty()) {
    int src = p.ir_src == 1 ? 1 : 0, k = -1;
    for (int t = 0; t < 2 && k < 0; t++) {
      const int s2 = t == 0 ? src : 1 - src;
      k = Library::find(w->libs[s2], p.ir_path, p.ir_display, p.ir_file);
      if (k >= 0) src = s2;
    }
    w->norm[P_SOURCE] = (float)src;
    w->changed[P_SOURCE] = 1;
    b.lib = w->libs[src];
    if (k >= 0) { b.want = k; b.saved = LibEntry(); b.error.clear(); }
    else if (!w->scanned) { b.want = -1; b.saved.path = p.ir_path; b.saved.display = p.ir_display; b.saved.file = p.ir_file; b.error.clear(); }
    else {                                     // gone: the settings apply, the IR in use stays
      b.want = b.loaded.path.empty() ? -1 : Library::find(b.lib, b.loaded.path, "", "");
      w->mp.error = "IR not found: " + (p.ir_file.empty() ? std::string("?") : p.ir_file);
    }
  }
  if (b.want >= 0) b.loading = true;           // (the shape may have changed too: prepared again, crossfaded in)
  requested(b);
  kick(w, false);
  browser_changed(w);
  w->update_display = 1;
}

static void mp_choose(Inst* w, int idx) {      // select list[idx] and apply it
  PresetData p;
  {
    std::lock_guard<std::mutex> lk(w->mu);
    MyPresets& m = w->mp;
    if (idx < 0 || idx >= (int)m.list.size()) return;
    m.want = idx; m.sel_name = m.list[idx].name; m.active = true; m.error.clear(); m.notice.clear(); m.armed_name.clear();
    p = m.list[idx];
    w->changed[P_PRESET] = 1;
    w->update_display = 1;
  }
  w->preset_clean = false;                     // (a My Preset: your own settings, kept whatever IR follows)
  mp_apply(w, p);
}

static void open_presets_page(Inst* w);        // below

static void set_mp_param(Inst* w, int i, float v) {
  bool phone = false;
  {
    std::lock_guard<std::mutex> lk(w->mu);
    MyPresets& m = w->mp;
    const int n = (int)m.list.size();
    const auto now = std::chrono::steady_clock::now();
    if (i == P_MP_SAVE) {
      if (v >= 0.5f) {
        m.pending = mp_snapshot(w);
        m.save_req = true;
        m.notice = "Saving..."; m.notice_until = now + std::chrono::seconds(3);
        kick(w, false);
      }
      w->changed[i] = 1;
    } else if (i == P_MP_DELETE) {
      if (v >= 0.5f) {
        if (m.want >= 0 && m.want < n) {
          const std::string name = m.list[m.want].name;
          const auto armed_at = m.armed_until - std::chrono::seconds(kArmSeconds);
          if (m.armed_name == name && now < armed_at + std::chrono::milliseconds(kTapMs)) {}   // (the same tap, twice)
          else if (m.armed_name == name && now < m.armed_until) { m.armed_name.clear(); m.delete_name = name; kick(w, false); }
          else { m.armed_name = name; m.armed_until = now + std::chrono::seconds(kArmSeconds); }
        } else {
          m.notice = "Pick a saved preset first"; m.notice_until = now + std::chrono::seconds(3);
        }
      }
      w->changed[i] = 1;
    } else if (i == P_MP_PHONE) {
      phone = v >= 0.5f;
      if (phone) m.show_addr = true;
      w->changed[i] = 1;
    }
    w->update_display = 1;
  }
  if (phone) open_presets_page(w);
}

// what the phone page asks of the plugin (its server thread; each takes mu itself)
static PresetsWebHooks mp_hooks(Inst* w) {
  PresetsWebHooks h;
  h.save = [w](const std::string& name, bool overwrite) -> std::string {
    PresetData p;
    {
      std::lock_guard<std::mutex> lk(w->mu);
      p = mp_snapshot(w);
    }
    p.name = name;
    std::string err;
    if (!Presets::save(p, overwrite, &err)) return err;
    std::lock_guard<std::mutex> lk(w->mu);
    MyPresets& m = w->mp;
    m.sel_name = name; m.active = true; m.refresh = true;
    m.notice = "Saved: " + name; m.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    w->changed[P_PRESET] = 1;
    kick(w, false);
    w->update_display = 1;
    return "";
  };
  h.load = [w](const std::string& name) -> std::string {
    PresetData p;
    if (!Presets::load(name, &p)) return "Preset not found";
    {
      std::lock_guard<std::mutex> lk(w->mu);
      MyPresets& m = w->mp;
      m.sel_name = p.name; m.active = true; m.refresh = true; m.error.clear();
      m.want = -1;
      for (int k = 0; k < (int)m.list.size(); k++) if (ieq(m.list[k].name, p.name)) m.want = k;
      w->changed[P_PRESET] = 1;
      kick(w, false);
    }
    mp_apply(w, p);
    return "";
  };
  h.remove = [w](const std::string& name) -> std::string {
    std::string err;
    if (!Presets::remove(name, &err)) return err;
    std::lock_guard<std::mutex> lk(w->mu);
    MyPresets& m = w->mp;
    if (ieq(m.sel_name, name)) { m.sel_name.clear(); m.want = -1; m.active = false; }
    m.refresh = true;
    kick(w, false);
    w->changed[P_PRESET] = 1; w->update_display = 1;
    return "";
  };
  h.rename = [w](const std::string& from, const std::string& to) -> std::string {
    std::string err;
    if (!Presets::rename(from, to, &err)) return err;
    std::lock_guard<std::mutex> lk(w->mu);
    MyPresets& m = w->mp;
    if (ieq(m.sel_name, from)) m.sel_name = to;
    m.refresh = true;
    kick(w, false);
    w->changed[P_PRESET] = 1; w->update_display = 1;
    return "";
  };
  h.current = [w]() -> std::string {
    std::lock_guard<std::mutex> lk(w->mu);
    return w->mp.sel_name;
  };
  return h;
}

static Tone3000* ensure_t3k(Inst* w) {
  {
    std::lock_guard<std::mutex> lk(w->mu);
    if (w->t3k) return w->t3k.get();
  }
  std::unique_ptr<Tone3000> made(new Tone3000([w](const std::string& path, const std::string&) {
    std::lock_guard<std::mutex> lk(w->mu);
    w->select_after_scan = path;
    w->scanning = true;
    kick(w, true);
    w->update_display = 1;
  }));
  made->set_route([w](const std::string& method, const std::string& path, const std::map<std::string, std::string>& args,
                      WebReply* out) { return presets_web_route(mp_hooks(w), method, path, args, out); });
  std::lock_guard<std::mutex> lk(w->mu);
  if (!w->t3k) w->t3k = std::move(made);
  return w->t3k.get();
}
static void open_presets_page(Inst* w) {
  ensure_t3k(w)->start("", false);             // no TONE3000 sign-in is wanted: no libcurl needed
  w->update_display = 1;
}

// a tap on the IR List / My Presets buttons, the page buttons, or a row
static void set_menu_param(Inst* w, int i, float v) {
  int pick = -1;                               // a My Preset to load (mp_choose takes mu itself)
  bool row_ir = false;                         // an IR List row: an IR picked
  {
    std::lock_guard<std::mutex> lk(w->mu);
    const int m = w->menu, sz = menu_page_size(m);
    if (i == P_IR_LIST || i == P_MP_LIST) {
      if (v >= 0.5f) {
        const int open = i == P_IR_LIST ? 1 : 2;
        w->menu = open;
        w->marq_key = -1;                      // (long names start from their beginning)
        w->mp.show_addr = false;
        w->mp.armed_name.clear();
        if (open == 2) { w->mp.refresh = true; kick(w, false); }   // (files added on a computer show up)
        menu_goto_selected(w);
      }
    } else if (i == P_MENU_CLOSE) {
      if (v >= 0.5f) { w->menu = 0; w->mp.armed_name.clear(); w->ir.armed_path.clear(); }
    } else if (i == P_MENU_PREV || i == P_MENU_NEXT) {
      if (v >= 0.5f && m) {
        const int pages = menu_pages(w, m);
        w->menu_page = ((w->menu_page + (i == P_MENU_NEXT ? 1 : -1)) % pages + pages) % pages;
      }
    } else if (is_row(i)) {
      const int r = i - P_ROW1, idx = w->menu_page * sz + r;
      if (m && r < sz && idx < menu_count(w, m)) {
        if (m == 1) {                          // the IR: as stepping to it (the list stays open: try another)
          Browser& b = w->ir;
          b.want = idx; b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false);
          browser_changed(w);
          row_ir = true;
        } else {
          pick = idx;
        }
      }
    }
    w->changed[i] = 1;                         // (a button or row the host toggled goes back to what it is)
    w->changed[P_MENU] = 1;
    w->update_display = 1;
  }
  if (pick >= 0) mp_choose(w, pick);
  if (row_ir) ir_picked(w);
}

static void set_parameter(AEffect* e, int32_t i, float v) {
  Inst* w = (Inst*)e->object;
  if (i < 0 || i >= P_COUNT) return;
  v = clamp01(v);
  w->host_view[i] = v;                         // what the host now shows (notify() tells it when that changes)
  if (i == P_RESCAN || i == P_T3K_INFO) {
    if (i == P_RESCAN && v >= 0.5f) {
      std::lock_guard<std::mutex> lk(w->mu);
      w->scanning = true;
      w->mp.refresh = true;                    // (My Presets: files added or renamed on a computer show up too)
      kick(w, true);
      w->changed[P_RESCAN] = 1;
      w->update_display = 1;
    }
    return;
  }
  if (is_display(i)) return;                   // read-only: what the plugin shows
  if (is_long(i)) {                            // LONG IR
    if (i == P_LONG_IR_INFO) return;           // read-only
    {
      std::lock_guard<std::mutex> lk(w->mu);   // (kick() too: the worker reads work_req under mu)
      const bool was = long_ir_on(w);
      // The page's LONG IR button is this setting itself (MPC switches it, as REVERSE): one tap on, one off; the
      // line under it says CPU heavy while it's on. (It was two taps, the first a warning, on a separate tap
      // parameter: on the device each tap arrived twice, so it came on at once and warned again on the way off.)
      if (i == P_LONG_IR) {
        if (!platform::kGen2) w->norm[P_LONG_IR] = v >= 0.5f ? 1.0f : 0.0f;
      } else if (v >= 0.5f && !platform::kGen2) {   // the old tap parameter (a host's list): a toggle, one per tap
        const auto now = std::chrono::steady_clock::now();
        if (now - w->long_tap_at >= std::chrono::milliseconds(kTapMs)) {
          w->norm[P_LONG_IR] = w->norm[P_LONG_IR] > 0.5f ? 0.0f : 1.0f;
          w->long_tap_at = now;
        }
      }
      if (long_ir_on(w) != was) {                // prepared again at the new quality (crossfaded in)
        if (w->ir.want >= 0) w->ir.loading = true;
        kick(w, false);
      }
      w->changed[P_LONG_IR] = 1; w->changed[P_LONG_IR_TAP] = 1; w->changed[P_LONG_IR_INFO] = 1;
      w->update_display = 1;
    }
    return;
  }
  if (is_mp(i)) { if (i != P_MP_INFO) set_mp_param(w, i, v); return; }
  if (is_menu(i)) { if (i != P_MENU && i != P_MENU_TITLE) set_menu_param(w, i, v); return; }
  if (i == P_PRESET) { apply_preset(w, (int)lroundf(v * kPresetCount)); return; }
  if (i == P_SOURCE) {                         // the other list; what plays stays (found there if it's in it)
    w->norm[i] = v;
    std::lock_guard<std::mutex> lk(w->mu);
    Browser& b = w->ir;
    b.lib = w->libs[source_of(w)];
    b.want = b.loaded.path.empty() ? -1 : Library::find(b.lib, b.loaded.path, "", "");
    b.error.clear();
    if (w->menu == 1) menu_goto_selected(w);
    browser_changed(w);
    w->update_display = 1;
    return;
  }
  if (!is_browser(i)) {
    if (is_setting(i) && std::fabs(v - w->norm[i]) > 1e-6f) w->preset_clean = false;   // (you changed the preset)
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
  bool browse = false, picked = false;         // picked: an IR chosen (My IRs: maybe back to Init, see back_to_init)
  {
    std::lock_guard<std::mutex> lk(w->mu);
    Browser& b = w->ir;
    const int n = (int)b.lib.size();
    if (i == P_IR_SRC || i == P_IR_INFO || i == P_IR_PACK) return;   // read-only
    if (i == P_IR) {
      if (n > 0) {
        b.want = n > 1 ? (int)lroundf(v * (n - 1)) : 0;
        b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false); picked = true;
        w->changed[P_IR_SRC] = 1;
      }
    } else if (i == P_IR_PREV || i == P_IR_NEXT) {
      if (v >= 0.5f && n > 0) {
        if (b.want < 0) b.want = i == P_IR_NEXT ? 0 : n - 1;   // from "no IR": the first / the last
        else b.want = ((b.want + (i == P_IR_NEXT ? 1 : -1)) % n + n) % n;
        b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false); picked = true;
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
        b.saved = LibEntry(); b.error.clear(); requested(b); kick(w, false); picked = true;
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
        const auto armed_at = b.armed_until - std::chrono::seconds(kArmSeconds);
        if (b.armed_path == path && now < armed_at + std::chrono::milliseconds(kTapMs)) {}   // (the same tap, twice)
        else if (b.armed_path == path && now < b.armed_until) { b.armed_path.clear(); b.delete_req = true; kick(w, false); }
        else { b.armed_path = path; b.armed_until = now + std::chrono::seconds(kArmSeconds); }
      }
      w->changed[i] = 1;
    }
    w->update_display = 1;
  }
  if (picked) ir_picked(w);
  if (browse) open_tones(w);
}

static bool menu_row_on(const Inst* w, int r) {   // holds mu: the row shows the item in use
  const int m = w->menu, sz = menu_page_size(m), idx = w->menu_page * sz + r;
  return m && r < sz && idx < menu_count(w, m) && idx == menu_selected(w, m) && (m == 1 || w->mp.active);
}

static float get_parameter(AEffect* e, int32_t i) {
  Inst* w = (Inst*)e->object;
  if (i < 0 || i >= P_COUNT) return 0;
  if (i == P_IR || i == P_IR_SRC) {
    std::lock_guard<std::mutex> lk(w->mu);
    return i == P_IR ? browser_norm(w->ir) : src_norm(w->ir);
  }
  if (is_menu(i)) {
    if (i == P_MENU) return w->menu / 2.0f;
    if (!is_row(i)) return 0;
    std::lock_guard<std::mutex> lk(w->mu);
    return menu_row_on(w, i - P_ROW1) ? 1.0f : 0.0f;
  }
  if (is_mp(i)) return 0;
  if (i == P_LONG_IR) return long_ir_on(w) ? 1.0f : 0.0f;
  if (i == P_LONG_IR_TAP || i == P_LONG_IR_INFO) return 0;
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
  // UpdateDisplay: MPC answers each one by reading every parameter again and rebuilding its screen (mpc-vst-plugins
  // wrapper/vst2_wrap.c), the slow part of a preset pick on the device. So they're paced: at most one per kDispGapMs,
  // and while an IR is being prepared one waits (up to kDispHoldMs), so a pick redraws once, with the new IR's name.
  const auto now = std::chrono::steady_clock::now();
  if (w->update_display.exchange(0) && !w->disp_wait) { w->disp_wait = true; w->disp_since = now; }
  bool disp = false;
  if (w->disp_wait) {
    auto ms = [&](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(now - t).count(); };
    bool loading = true;                       // (unknown while the lock is busy: treated as loading)
    {
      std::unique_lock<std::mutex> lk(w->mu, std::try_to_lock);   // never wait on the audio thread
      if (lk.owns_lock()) loading = w->ir.loading;
    }
    disp = ms(w->disp_last) >= kDispGapMs && (!loading || ms(w->disp_since) >= kDispHoldMs);
  }
  if (disp) { w->disp_wait = false; w->disp_last = now; }
  if (disp && w->menu.load()) {                // a list is open: its title and rows are read again
    w->changed[P_MENU_TITLE] = 1;
    for (int r = P_ROW1; r <= P_ROW_LAST; r++) w->changed[r] = 1;
  }
  for (int i = 0; i < P_COUNT; i++) {
    if (!w->changed[i].exchange(0)) continue;  // (values go at once: knobs move and the popup closes without waiting)
    float v = 0;
    if (is_display(i) && i != P_PLAYHEAD && i != P_SONICS) v = w->wave[i - P_WAVE0] / (float)(8 * ReverbIR::kTones - 1);
    if (i == P_IR || i == P_IR_SRC || is_row(i)) {
      std::unique_lock<std::mutex> lk(w->mu, std::try_to_lock);   // never wait on the audio thread
      if (!lk.owns_lock()) { w->changed[i] = 1; continue; }
      v = i == P_IR ? browser_norm(w->ir) : i == P_IR_SRC ? src_norm(w->ir) : menu_row_on(w, i - P_ROW1) ? 1.0f : 0.0f;
    }
    if (i == P_MENU) v = w->menu / 2.0f;
    if (i == P_LONG_IR) v = long_ir_on(w) ? 1.0f : 0.0f;
    if ((disp || w->disp_wait) && v == w->host_view[i].load()) continue;   // the host has it; an UpdateDisplay redraws its text
    w->host_view[i] = v;
    w->master(&w->fx, audioMasterAutomate, i, 0, nullptr, v);
  }
  if (disp) w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, nullptr, 0);
}

static void process_replacing(AEffect* e, float** in, float** out, int32_t n) {
  Inst* w = (Inst*)e->object;
  if (w->master && sync_on(w)) {               // SYNC: the tempo, every block (MPC answers at once; a tempo change glides)
    const VstTimeInfo* ti = (const VstTimeInfo*)w->master(&w->fx, audioMasterGetTime, 0, kVstTempoValid, nullptr, 0);
    if (ti && (ti->flags & kVstTempoValid) && ti->tempo > 0) {
      w->bpm = ti->tempo;
      if (!w->tempo_ok.exchange(true)) { w->changed[P_PREDELAY] = 1; w->update_display = 1; }   // (the "@120" goes)
    }
    sync_predelay(w);
  }
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
  return (i == P_IR || i == P_IR_INFO || i == P_IR_PACK || i == P_T3K_INFO || i == P_SONICS || i == P_MP_INFO ||
          i == P_MENU_TITLE || is_row(i) || i == P_LONG_IR_INFO) ? kTextLong : kText;
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
    return w->t3k ? w->t3k->status() : "Picks IRs on your phone";
  }
  if (i == P_MP_INFO) {
    std::string addr, tst;
    {
      std::lock_guard<std::mutex> lk(w->mu);
      if (w->t3k) { addr = w->t3k->address(); tst = w->t3k->status(); }
    }
    std::lock_guard<std::mutex> lk(w->mu);
    const MyPresets& m = w->mp;
    const int n = (int)m.list.size();
    const auto now = std::chrono::steady_clock::now();
    if (m.want >= 0 && m.want < n && m.armed_name == m.list[m.want].name && now < m.armed_until)
      return "Tap DELETE again to delete it permanently";
    if (!m.notice.empty() && now < m.notice_until) return m.notice;
    if (m.show_addr) {                           // NAME ON PHONE: where to point the phone's browser
      if (!addr.empty()) return "Phone: " + addr + "/presets";
      if (!tst.empty() && tst != "Page closed") return tst;
    }
    if (!m.error.empty()) return m.error;
    if (!m.listed) return "Searching...";
    if (n == 0) return "Tap SAVE to keep the current sound";
    if (!m.active || m.want < 0 || m.want >= n) return "Tap a preset to load it";
    const PresetData& p = m.list[m.want];
    if (p.ir_file.empty()) return "No IR";
    return "IR: " + (p.ir_src == 0 ? factory_name(stem_of(p.ir_file)) : stem_of(p.ir_file));
  }
  if (i == P_MENU) return p.opts[std::max(0, std::min(2, w->menu.load()))];
  if (i == P_LONG_IR_INFO) {
    if (platform::kGen2) return "Gen 2 \xC2\xB7 always on";
    return long_ir_on(w) ? "On \xC2\xB7 CPU heavy \xC2\xB7 20 s" : "Off \xC2\xB7 IRs up to 5 s";
  }
  if (i == P_LONG_IR) return long_ir_on(w) ? "On" : "Off";
  if (i == P_MENU_TITLE || is_row(i)) {
    std::lock_guard<std::mutex> lk(w->mu);
    const int m = w->menu, sz = menu_page_size(m), n = menu_count(w, m);
    if (!m) return "";
    if (i == P_MENU_TITLE) {
      if (n == 0) {
        if (m == 1) return !w->scanned || w->scanning ? "Searching..." : "No IRs here";
        return w->mp.listed ? "No saved presets" : "Searching...";
      }
      const int first = w->menu_page * sz + 1, last = std::min(n, first + sz - 1);
      snprintf(buf, sizeof buf, "%s %d-%d of %d", m == 1 ? "IRs" : "Presets", first, last, n);
      return buf;
    }
    const int r = i - P_ROW1, idx = w->menu_page * sz + r;
    if (r >= sz || idx >= n) return "";
    return marq_window(menu_row_text(w, m, idx), marq_step_now(w));
  }
  if (i == P_PRESET) {                           // a My Preset was applied last: the PRESET field names it
    std::lock_guard<std::mutex> lk(w->mu);
    if (w->mp.active && !w->mp.sel_name.empty()) return "My: " + w->mp.sel_name;
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
      const std::string pack = s == std::string::npos ? cur->pack : cur->pack.substr(s + 1);
      const char* best = source_of(w) == 0 ? best_on(factory_name(stem_of(cur->file))) : nullptr;
      return best ? pack + " · best on " + best : pack;   // a factory IR: what it suits
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
  if (i == P_PREDELAY && sync_on(w)) {         // SYNC: the note value ("1/8 D"); without a tempo from the host, at 120
    const int k = sync_note(w);
    return k == 0 || w->tempo_ok ? std::string(kNotes[k].name) : std::string(kNotes[k].name) + " @120";
  }
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
    if (is_browser(i) || is_display(i) || is_mp(i) || is_menu(i) || PARAMS[i].momentary || i == P_T3K_INFO || PARAMS[i].popup_of >= 0 ||
        i == P_LONG_IR_INFO) continue;
    snprintf(buf, sizeof buf, "%s=%.6g\n", PARAMS[i].key, real_of(i, w->norm[i]));
    s += buf;
  }
  std::lock_guard<std::mutex> lk(w->mu);
  const Browser& b = w->ir;
  const LibEntry& e = (b.want >= 0 && b.want < (int)b.lib.size()) ? b.lib[b.want] : b.saved;
  s += "ir_path=" + e.path + "\nir_display=" + e.display + "\nir_file=" + e.file + "\n";
  if (!w->mp.sel_name.empty()) s += "mp_name=" + w->mp.sel_name + "\nmp_active=" + (w->mp.active ? "1" : "0") + "\n";
  if (w->preset_clean) s += "preset_clean=1\n";   // (the settings are still the factory preset's)
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
  std::string mp_name;
  bool mp_active = false, clean = false;
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
    if (k == "mp_name") { mp_name = v; continue; }
    if (k == "mp_active") { mp_active = v == "1"; continue; }
    if (k == "preset_clean") { clean = v == "1"; continue; }
    for (int i = 0; i < P_COUNT; i++) {
      if (is_browser(i) || is_mp(i) || is_menu(i) || PARAMS[i].momentary || i == P_LONG_IR_INFO || k != PARAMS[i].key) continue;
      const param_t& p = PARAMS[i];
      float r = (float)atof(v.c_str());
      w->norm[i] = norm_of(i, r);
      apply_param(w, i);
    }
  }
  w->preset_clean = clean;
  w->chunk_at = std::chrono::steady_clock::now();   // (programs the host picks in the next 2 s: its saved one, ignored)
  std::lock_guard<std::mutex> lk(w->mu);
  w->mp.sel_name = mp_name;                    // the selected My Preset: shown again, not applied again (the project holds its sound)
  w->mp.active = mp_active && !mp_name.empty();
  w->mp.want = -1;
  for (int k = 0; k < (int)w->mp.list.size(); k++) if (!mp_name.empty() && ieq(w->mp.list[k].name, mp_name)) w->mp.want = k;
  w->ir.saved = saved;
  {                                            // what MPC's menu shows: the project's My IR, else its PRESET
    const int pi = source_of(w) == 1 ? prog_of_ir(w, saved.path) : -1;
    w->cur_prog = pi >= 0 ? pi : prog_of_preset((int)lroundf(w->norm[P_PRESET] * kPresetCount));
  }
  w->ir.lib = w->libs[source_of(w)];           // the project's list (Factory / My IRs)
  w->ir.error.clear();
  if (w->scanned) resolve(w->ir);
  requested(w->ir);
  kick(w, !w->scanned);
  for (int i = 0; i < P_COUNT; i++) { w->changed[i] = 1; w->host_view[i] = -1.0f; }   // (all reported again)
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
    case effCanBeAutomated: return idx >= 0 && idx < P_COUNT && !is_display(idx) && idx != P_PRESET_OPEN && idx != P_MENU && idx != P_MENU_TITLE &&
                                   !is_long(idx);   // (LONG IR is a device setting, never automation)
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
    case effCanDo:                             // MIDI: note-ons restart the waveform's playhead (if the host sends them)
      if (p && (!strcmp((const char*)p, "receiveVstEvents") || !strcmp((const char*)p, "receiveVstMidiEvent"))) return 1;
      return -1;
    case effProcessEvents: {
      const VstEvents* ev = (const VstEvents*)p;
      if (!ev || !w->eng) return 1;
      for (int32_t k = 0; k < ev->numEvents; k++) {
        const VstEvent* e = ev->events[k];
        if (!e || e->type != kVstMidiType) continue;
        const unsigned char* m = (const unsigned char*)e->data + 8;   // VstMidiEvent: noteLength, noteOffset, midiData[4]
        if ((m[0] & 0xF0) == 0x90 && m[2] > 0) w->eng->note_on();
      }
      return 1;
    }
    case effSetProgram: {                      // MPC's PRESET menu: a factory preset (as our PRESET list) or a My IR
      if (v < 0 || v >= program_count(w) || (int)v == w->cur_prog) return 1;   // (the current one, as hosts re-select: kept)
      // A project's state just arrived: a host may now re-select the program it saved, and My IRs' places move when IRs
      // are added or deleted, so the number could name another sound. The project's own state is the sound: ignored.
      if (std::chrono::steady_clock::now() - w->chunk_at < std::chrono::seconds(2)) return 1;
      const int k = preset_of_prog((int)v);
      if (k >= 0) {
        set_parameter(e, P_PRESET, (float)k / kPresetCount);
        w->host_view[P_PRESET] = -1.0f; w->changed[P_PRESET] = 1;    // (the host didn't write it: told)
      } else {
        pick_ir_program(w, (int)v - kProgramCount);
      }
      w->cur_prog = (int)v;
      return 1;
    }
    case effGetProgram: return w->cur_prog;
    case effGetProgramName:
      if (p) copy_text(p, program_name(w, w->cur_prog), 24);
      return 1;
    case effGetProgramNameIndexed:
      if (!p || idx < 0 || idx >= program_count(w)) return 0;
      copy_text(p, program_name(w, idx), 24);
      return 1;
    case effGetChunk: { int len = get_chunk(w); *(void**)p = (void*)w->chunk.c_str(); return len; }
    case effSetChunk: return (v > 0 && p) ? (set_chunk(w, (const char*)p, (size_t)v) ? 1 : 0) : 0;
    default: return 0;
  }
}

extern "C" __attribute__((visibility("default"))) AEffect* VSTPluginMain(audioMasterCallback master) {
  Inst* w = new (std::nothrow) Inst();
  if (!w) return nullptr;
  w->master = master;
  for (int i = 0; i < P_COUNT; i++) { w->changed[i] = 0; w->norm[i] = def_norm(i); w->host_view[i] = -1.0f; }
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
  {                                            // MPC's PRESET menu: Init, the factory presets, then My IRs, read from the
    std::vector<LibEntry> mine;                // folder index the last scan left (fast: no walk of the cards; a first
    if (Library::scan_quick("reverbs", ".wav", &mine)) {   // load has none yet, the next one lists them)
      if ((int)mine.size() > kMaxProgIrs) mine.resize(kMaxProgIrs);
      w->prog_irs = mine;
    }
  }
  e->numPrograms = program_count(w);
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
