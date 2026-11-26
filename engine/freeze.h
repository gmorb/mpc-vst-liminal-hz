// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* freeze.h -- FREEZE: the reverb as it sounds now, held as a steady pad (mpc-vst-liminal-hz).
 *
 * The wet signal is kept in a ring (the last kRing samples). Freezing takes its spectrum: the average power of three
 * Hann windows of kN samples (half-overlapped) per channel, one FFT per audio block, so the analysis spreads over 6
 * blocks. The pad is that spectrum with new random phases each frame: windowed (Hann) and overlap-added at a hop of
 * kN / 4, which gives a steady, smooth sound with the room's colour and pitches (no loop, no seam). Its level is the
 * level of the wet signal's last kLevel samples. Each hop's next frame is made ahead, one channel per block.
 * All of it runs on the audio thread, allocation-free after the constructor.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "pffft.h"

namespace irrev {

class SpectralFreeze {
 public:
  static constexpr int kN = 8192, kHop = kN / 4, kRing = 2 * kN, kLevel = 4096, kBins = kN / 2;
  static constexpr int kTable = 4096;            // phases: a sine/cosine table

  SpectralFreeze() {
    st_ = pffft_new_setup(kN, PFFFT_REAL);
    buf_ = (float*)pffft_aligned_malloc(kN * sizeof(float));
    work_ = (float*)pffft_aligned_malloc(kN * sizeof(float));
    for (int c = 0; c < 2; c++) {
      ring_[c].assign(kRing, 0.0f); snap_[c].assign(kRing, 0.0f); ola_[c].assign(kN, 0.0f); next_[c].assign(kN, 0.0f);
      pow_[c].assign(kBins, 0.0f); mag_[c].assign(kBins, 0.0f);
    }
    win_.resize(kN);
    for (int i = 0; i < kN; i++) win_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / kN));   // periodic Hann
    cos_.resize(kTable); sin_.resize(kTable);
    for (int i = 0; i < kTable; i++) { cos_[i] = (float)std::cos(2 * M_PI * i / kTable); sin_[i] = (float)std::sin(2 * M_PI * i / kTable); }
  }
  ~SpectralFreeze() { pffft_aligned_free(buf_); pffft_aligned_free(work_); pffft_destroy_setup(st_); }
  SpectralFreeze(const SpectralFreeze&) = delete;
  SpectralFreeze& operator=(const SpectralFreeze&) = delete;

  // the wet signal as heard (call every block while not frozen)
  void capture(const float* l, const float* r, int n) {
    for (int i = 0; i < n; i++) { ring_[0][pos_] = l[i]; ring_[1][pos_] = r[i]; pos_ = (pos_ + 1) % kRing; }
  }
  // freeze what the ring holds: the analysis runs over the next blocks (step()); ready() once the pad has it
  void start() {
    for (int c = 0; c < 2; c++) {                // oldest first
      std::copy(ring_[c].begin() + pos_, ring_[c].end(), snap_[c].begin());
      std::copy(ring_[c].begin(), ring_[c].begin() + pos_, snap_[c].begin() + (kRing - pos_));
      std::fill(pow_[c].begin(), pow_[c].end(), 0.0f);
    }
    an_ = 0;
  }
  bool analysing() const { return an_ >= 0; }
  bool ready() const { return have_; }           // a spectrum is held (the pad can play)
  void stop() { running_ = false; have_ = false; an_ = -1; }   // the pad has faded out: nothing more to make

  // one block's work, then n (<= kHop, a divisor of it) samples of the pad, added to out (scaled by gain per sample)
  void render(float* out_l, float* out_r, const float* gain, int n) {
    if (an_ >= 0) analyse_step();
    if (!running_) return;
    if (make_ >= 0) { make_frame(make_); make_ = make_ == 0 ? 1 : -1; }   // next hop's frame, a channel a block
    for (int i = 0; i < n; i++) {
      if (hp_ == 0) {                            // a hop: the next frame goes in (made ahead), the one after is begun
        for (int c = 0; c < 2; c++)
          for (int k = 0; k < kN; k++) ola_[c][(rp_ + k) % kN] += next_[c][k];
        make_ = 0;
      }
      out_l[i] += gain[i] * ola_[0][rp_]; out_r[i] += gain[i] * ola_[1][rp_];
      ola_[0][rp_] = 0.0f; ola_[1][rp_] = 0.0f;
      rp_ = (rp_ + 1) % kN;
      hp_ = (hp_ + 1) % kHop;
    }
  }

 private:
  void analyse_step() {                          // one window of one channel; the last one makes the spectrum the pad's
    const int c = an_ / 3, w = an_ % 3;
    const float* x = snap_[c].data() + w * (kN / 2);
    for (int i = 0; i < kN; i++) buf_[i] = x[i] * win_[i];
    pffft_transform_ordered(st_, buf_, buf_, work_, PFFFT_FORWARD);
    for (int k = 1; k < kBins; k++) pow_[c][k] += buf_[2 * k] * buf_[2 * k] + buf_[2 * k + 1] * buf_[2 * k + 1];
    if (++an_ < 6) return;
    an_ = -1;
    for (int ch = 0; ch < 2; ch++) {
      for (int k = 0; k < kBins; k++) mag_[ch][k] = std::sqrt(pow_[ch][k] / 3.0f);
      double e = 0;                              // the level to hold: the ring's last kLevel samples
      for (int i = kRing - kLevel; i < kRing; i++) e += (double)snap_[ch][i] * snap_[ch][i];
      target_[ch] = (float)std::sqrt(e / kLevel);
      gain_[ch] = -1.0f;                         // (set from the first frame made: every frame has the same energy)
    }
    have_ = true;
    if (!running_) {                             // from silence: the first frame now, the pad starts with it
      running_ = true;
      for (int ch = 0; ch < 2; ch++) std::fill(ola_[ch].begin(), ola_[ch].end(), 0.0f);
      make_frame(0); make_frame(1);
      hp_ = 0; make_ = -1;
    } else {                                     // already playing (a fading pad frozen again): the new one from the next hop
      make_ = 0;
    }
  }
  void make_frame(int c) {                       // random phases on the held magnitudes; windowed, at the held level
    buf_[0] = 0.0f; buf_[1] = 0.0f;              // (DC, Nyquist)
    for (int k = 1; k < kBins; k++) {
      rng_ = rng_ * 1664525u + 1013904223u;
      const int p = (int)(rng_ >> 20) & (kTable - 1);
      buf_[2 * k] = mag_[c][k] * cos_[p];
      buf_[2 * k + 1] = mag_[c][k] * sin_[p];
    }
    pffft_transform_ordered(st_, buf_, buf_, work_, PFFFT_BACKWARD);
    if (gain_[c] < 0.0f) {                       // Hann^2 overlap-added at kN / 4: 1.5 x a frame's power
      double e = 0;
      for (int i = 0; i < kN; i++) e += (double)buf_[i] * buf_[i];
      const double frame_rms = std::sqrt(e / kN);
      gain_[c] = frame_rms > 1e-20 ? (float)(target_[c] / (std::sqrt(1.5) * frame_rms)) : 0.0f;
    }
    for (int i = 0; i < kN; i++) next_[c][i] = buf_[i] * win_[i] * gain_[c];
  }

  PFFFT_Setup* st_ = nullptr;
  float* buf_ = nullptr;
  float* work_ = nullptr;
  std::vector<float> ring_[2], snap_[2], ola_[2], next_[2], pow_[2], mag_[2], win_, cos_, sin_;
  float target_[2] = {0, 0}, gain_[2] = {0, 0};
  int pos_ = 0, an_ = -1, rp_ = 0, hp_ = 0, make_ = -1;
  bool have_ = false, running_ = false;
  uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace irrev
