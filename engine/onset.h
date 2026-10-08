// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* onset.h -- note onsets in the input, for the waveform's playhead (mpc-vst-liminal-hz).
 *
 * A level jump alone missed soft attacks and notes played over a long release (pads: the old detector caught none in
 * tests). This one looks at the spectrum: a 1024-point FFT per 128-sample block, log magnitudes of the bins from 60 Hz
 * to 8 kHz, each widened to its neighbours (a voice's vibrato or detune stays in its own bin's reach). Two detectors:
 *   - fast (SuperFlux-like): how far bins rise over the louder of their last 3 frames, summed, over a threshold that
 *     follows the music: hits, plucks, a new note over a held one (its partials are new bins);
 *   - slow: 10% or more of the sounding bins now 6 dB over everything they did in the last 0.26 s: a pad fading in, a new
 *     chord, a note over a release that had died down. A beat between detuned voices, tremolo or vibrato only comes
 *     back to where it already was. One trigger per rise (armed again once the bins settle).
 * A bin is never counted from more than 12 dB under its recent peak (a beat's notch is not a note). Either detector
 * fires a note, then nothing for 120 ms. A note held legato with no change in sound can't be heard as a new note;
 * MIDI note-ons (when the host sends them to the plugin) cover that.
 * Real time: no allocation after construction; one FFT per block (pffft).
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include "pffft.h"

namespace irrev {

class OnsetDetector {
 public:
  static constexpr int N = 1024, kMaxBins = 256, kHist = 20;   // 20 history entries over 0.4 s
  explicit OnsetDetector(double rate) : rate_(rate) {
    setup_ = pffft_new_setup(N, PFFFT_REAL);
    buf_ = (float*)pffft_aligned_malloc(N * sizeof(float));
    work_ = (float*)pffft_aligned_malloc(N * sizeof(float));
    for (int i = 0; i < N; i++) win_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / N));
    k0_ = std::max(1, (int)std::lround(60.0 * N / rate));
    k1_ = std::min(std::min(N / 2 - 2, k0_ + kMaxBins), (int)std::lround(8000.0 * N / rate));
    nb_ = std::max(1, k1_ - k0_);
    for (auto& h : hist_) for (float& v : h) v = kFloor;
    for (auto& p : prev_) for (float& v : p) v = kFloor;
    hop_ = std::max(1, (int)std::lround(0.4 * rate / 128.0 / kHist));   // (blocks of 128 samples per history entry)
    quiet_len_ = std::max(1, (int)std::lround(0.12 * rate / 128.0));
  }
  ~OnsetDetector() {
    if (work_) pffft_aligned_free(work_);
    if (buf_) pffft_aligned_free(buf_);
    if (setup_) pffft_destroy_setup(setup_);
  }
  OnsetDetector(const OnsetDetector&) = delete;
  OnsetDetector& operator=(const OnsetDetector&) = delete;

  // one block of input (128 samples expected); true: a note started in it
  bool process(const float* l, const float* r, int n) {
    float pk = 0;
    for (int i = 0; i < n; i++) {
      ring_[pos_] = 0.5f * (l[i] + r[i]); pos_ = (pos_ + 1) % N;
      pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
    for (int i = 0; i < N; i++) buf_[i] = ring_[(pos_ + i) % N] * win_[i];
    pffft_transform_ordered(setup_, buf_, buf_, work_, PFFFT_FORWARD);
    float raw[kMaxBins + 2], L[kMaxBins];
    for (int j = -1; j <= nb_; j++) {
      const int k = k0_ + j;
      const double e = (double)buf_[2 * k] * buf_[2 * k] + (double)buf_[2 * k + 1] * buf_[2 * k + 1];
      raw[j + 1] = std::max(kFloor, (float)(10 * std::log10(e + 1e-30)) - kRef);
    }
    for (int j = 0; j < nb_; j++) L[j] = std::max(raw[j], std::max(raw[j + 1], raw[j + 2]));   // widened to its neighbours
    float flux = 0;
    int risen = 0, active = 0;
    for (int j = 0; j < nb_; j++) {
      float peak = kFloor;                                    // the bin's loudest of the last 0.26 s (not the newest 40 ms)
      for (int h = 2; h < kHistUse; h++) peak = std::max(peak, hist_[(hist_pos_ - 1 - h + 2 * kHist) % kHist][j]);
      const float floor12 = std::max(kFloor, peak - 12.0f);
      const float ref = std::max(std::max(prev_[0][j], std::max(prev_[1][j], prev_[2][j])), floor12);
      flux += std::max(0.0f, L[j] - ref);
      if (L[j] > kFloor + 10) { active++; if (L[j] >= peak + kRiseDb) risen++; }
    }
    flux *= 24.0f / (float)nb_;                               // (per 24 bins: the same scale at any rate)
    std::memmove(prev_[2], prev_[1], sizeof prev_[0]); std::memmove(prev_[1], prev_[0], sizeof prev_[0]);
    std::memcpy(prev_[0], L, sizeof(float) * nb_);
    if (++hop_count_ >= hop_) { hop_count_ = 0; std::memcpy(hist_[hist_pos_], L, sizeof(float) * nb_); hist_pos_ = (hist_pos_ + 1) % kHist; }
    const float thr = std::max(kFluxMin, mean_ + 3.0f * dev_);   // follows the music's own flux
    mean_ += 0.02f * (flux - mean_);
    dev_ += 0.02f * (std::fabs(flux - mean_) - dev_);
    if (quiet_ > 0) quiet_--;
    const bool slow_now = active >= 4 && risen * 100 >= kRisePct * active;
    if (risen * 300 < kRisePct * active) slow_armed_ = true;
    const bool fire = pk > 0.001f && quiet_ == 0 && (flux > thr || (slow_now && slow_armed_));   // over -60 dBFS
    if (slow_now) slow_armed_ = false;
    if (fire) { quiet_ = quiet_len_; slow_armed_ = false; }
    return fire;
  }

  static float kFluxMin, kRiseDb;                           // (tunable in tests)
  static int kRisePct, kHistUse;

 private:
  static constexpr float kFloor = -70.0f;                   // dB, about full scale: quieter bins count as silence
  static constexpr float kRef = 48.0f;                      // (FFT scale -> roughly dB under full scale)
  double rate_;
  PFFFT_Setup* setup_ = nullptr;
  float* buf_ = nullptr;
  float* work_ = nullptr;
  float win_[N];
  float ring_[N] = {};
  int pos_ = 0, k0_ = 1, k1_ = 2, nb_ = 1;
  float prev_[3][kMaxBins];
  float hist_[kHist][kMaxBins];
  int hist_pos_ = 0, hop_ = 1, hop_count_ = 0, quiet_len_ = 1;
  float mean_ = 0, dev_ = 0;
  int quiet_ = 0;
  bool slow_armed_ = true;
};

inline float OnsetDetector::kFluxMin = 20.0f;
inline int OnsetDetector::kRisePct = 10, OnsetDetector::kHistUse = 13;
inline float OnsetDetector::kRiseDb = 6.0f;

}  // namespace irrev
