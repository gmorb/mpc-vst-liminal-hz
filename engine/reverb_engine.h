// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* reverb_engine.h -- Liminal Hz's audio chain (mpc-vst-liminal-hz).
 *
 *   in L/R -> pre-delay -> convolution (LongConvolver: head on this thread, tail on a worker) -> low cut, high cut
 *          -> width (mid/side) -> mix with the dry signal -> output gain
 * Mono IR: L and R are convolved separately (the stereo image is kept). Stereo IR (2+ channels): the input's mono sum
 * feeds the IR's first two channels, L and R (how reverb IRs of spaces are recorded).
 * IRs are prepared off the audio thread (ReverbIR::make: resampled to the host rate as NAM's cab IRs are, cut to
 * kMaxSeconds and the Length setting, faded out at the end, normalised to unit energy) and handed over without
 * locks; a new IR crossfades in over kFade samples. Convolvers (and their worker threads) are never freed on the
 * audio thread: collect() frees them.
 */
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "long_convolver.h"

namespace irrev {

struct ReverbParams {
  std::atomic<float> mix{0.3f};           // 0..1, wet share
  std::atomic<float> predelay_ms{0.0f};   // 0..250
  std::atomic<float> lowcut_hz{80.0f};    // high-pass on the wet signal, 20..1000
  std::atomic<float> highcut_hz{12000.0f};// low-pass on the wet signal, 1000..20000
  std::atomic<float> width{1.0f};         // 0..2 (1 = as recorded)
  std::atomic<float> output_db{0.0f};     // -24..12
  std::atomic<float> feedback{0.0f};      // 0..0.9: the wet output fed back into the convolution (soft-limited)
};

// how the IR is shaped before use (all off the audio thread; Kilohearts-Convolver-style controls)
struct IRShape {
  double start = 0.0;      // trim: the share of the (capped) IR skipped at its start, 0..0.9
  double length = 1.0;     // trim: where it ends, as a share of the (capped) IR, 0.02..1 (after start)
  double fade_in = 0.0;    // share of the kept part faded in, 0..0.5
  double fade_out = 0.1;   // share of the kept part faded out, 0..0.5 (at least 32 samples: no click)
  double stretch = 1.0;    // 0.5..2: longer and lower (2) or shorter and higher (0.5)
  bool reverse = false;    // played backwards (after the 5 s cap: it swells into the IR's start)
  bool operator==(const IRShape& o) const {
    return start == o.start && length == o.length && fade_in == o.fade_in && fade_out == o.fade_out &&
           stretch == o.stretch && reverse == o.reverse;
  }
  bool operator!=(const IRShape& o) const { return !(*this == o); }
};

class ReverbIR {
 public:
  static constexpr double kMaxSeconds = 5.0;
  // channels[c][frame] at `rate`, shaped by `shape` (stretch, 5 s cap, reverse, start/length trim, fades, level)
  static std::unique_ptr<ReverbIR> make(const std::vector<std::vector<float>>& channels, double rate, double host_rate,
                                        const IRShape& shape, std::string* err,
                                        LongConvolver::Mode mode = LongConvolver::Threaded);
  static std::unique_ptr<ReverbIR> load(const std::string& wav_path, double host_rate, const IRShape& shape,
                                        std::string* err);
  bool stereo = false;                    // the IR has its own L and R
  size_t frames = 0;                      // at the host rate, as used
  std::unique_ptr<LongConvolver> l, r;
  long reported = 0;                      // misses already counted into ReverbEngine::misses() (audio thread)
  static constexpr int kCols = 12;
  static constexpr int kTones = 3;        // a column's tone: 0 dark, 1 balanced, 2 airy
  // per column: tone * 8 + level (level 0..7: the peak, 48 dB range; tone: how much high-frequency energy)
  std::array<uint8_t, kCols> wave{};
  std::string sonics;                     // e.g. "3.4 s decay · darkens · wide · diffuse" (computed at prep)
  float t60 = 0;                          // decay time, s (Schroeder: T20 x 3, else T10 x 6); 0 for a swell
  float bright = 0;                       // high-frequency share (first-difference energy / energy; white noise: 2)
  float width = 0;                        // 1 - L/R correlation (0 mono or identical channels, ~1 independent)
  float swell = 0;                        // a swelling IR (reversed, or peaking late): seconds to its peak; else 0
  std::array<uint8_t, kCols> tint{};      // each column's tone: 0 dark, 1 balanced, 2 airy (also in wave[])
};

class ReverbEngine {
 public:
  static constexpr int kFade = 4096;      // samples: a new IR crossfades in (~93 ms at 44.1 kHz)
  static constexpr int kMaxPredelay = 250;
  explicit ReverbEngine(double host_rate);
  ~ReverbEngine();
  ReverbParams params;
  void set_ir(std::unique_ptr<ReverbIR> ir);   // any thread but the audio thread; nullptr: no IR (fades out to dry)
  void collect();                              // not the audio thread: frees what the audio thread let go of
  void process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n);   // the audio thread
  long misses() const { return misses_.load(); }   // tail blocks that came late (each a brief gap in the tail)
  // the playhead: 0 hidden, 1..kPlay its position along the IR (pre-delay + length) since the last note
  static constexpr int kPlay = 24;        // the playhead's positions (finer than the display's columns: smooth)
  int playhead() const { return playhead_.load(std::memory_order_relaxed); }

 private:
  void block(const float* in_l, const float* in_r, float* out_l, float* out_r);   // 128 frames
  void retire(ReverbIR* p);
  void update_filters();

  double rate_;
  std::atomic<ReverbIR*> pending_{nullptr};
  std::atomic<bool> clear_{false};        // set_ir(nullptr): fade the current IR out, no IR after
  ReverbIR* cur_ = nullptr;               // audio thread
  ReverbIR* old_ = nullptr;               // fading out (audio thread)
  int fade_ = 0;
  float presence_ = 0.0f;
  float env_ = 0.0f;                      // the input's recent level (note detection, audio thread)
  long play_t_ = 1L << 40;                // samples since the last note
  std::atomic<int> playhead_{0};                 // 0 = no IR (the dry signal at full level), 1 = an IR (dry at 1 - mix)
  static constexpr int kRetire = 32;
  std::atomic<ReverbIR*> retired_[kRetire] = {};
  ReverbIR* hold_ = nullptr;              // the retire ring was full: kept until there's room
  std::atomic<long> misses_{0};
  long seen_misses_ = 0;
  // pre-delay (2 channels), filters (2 channels x 2 biquads), parameter cache
  std::vector<float> pd_[2];
  size_t pd_pos_ = 0;
  struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; float run(float x); };
  Biquad hp_[2], lp_[2];
  float lc_cache_ = -1, hc_cache_ = -1;
  // a host block that isn't a multiple of 128: through a FIFO (one block of latency)
  std::vector<float> fin_[2], fout_[2];
  int fifo_fill_ = 0;
  // scratch
  float wl_[128], wr_[128], ol_[128], or_[128], xl_[128], xr_[128];
  float fbl_[128] = {}, fbr_[128] = {};   // feedback: the last block's wet output, soft-limited
};

}  // namespace irrev
