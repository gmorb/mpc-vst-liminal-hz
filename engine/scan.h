// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* scan.h -- SCAN: the reverb's last few seconds, read back as a moving spectral pad (mpc-vst-liminal-hz).
 *
 * The same method as FREEZE (freeze.h), which keeps its sound clean: no grains, so nothing buzzes or aliases.
 * The wet signal (as heard) is always written to a ring of kRingSeconds. While SCAN is on, every hop (kN / 4) a
 * Hann window of kN samples is taken from the ring at `position` (0 = just behind now, 1 = `depth` seconds back:
 * the IR's length, so the glow over the painting points at the matching moment of the tail); its magnitude
 * spectrum is smoothed over time and played with fresh random phases, overlap-added. TEXTURE shapes the spectrum:
 *   0 .. 1/3   SMEAR: from a close ghost of the reverb to a slow, held wash (the time smoothing grows)
 *   1/3 .. 2/3 SHIMMER: an octave and a fifth above (and a little an octave below) come in: halos
 *   2/3 .. 1   WIND: the spectrum blurred across frequency and its bands drifting slowly in level: air, whoosh
 * Each frame is scaled to the level of what it was read from (smoothed alike), so the level stays that of the reverb.
 * Audio thread, allocation-free after the constructor: per hop, 2 forward and 2 inverse FFTs, one per block.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "pffft.h"

namespace irrev {

class SpectralScan {
 public:
  static constexpr int kN = 8192, kHop = kN / 4, kBins = kN / 2, kTable = 4096, kBands = 24;   // (FREEZE's size: smooth)
  static constexpr double kRingSeconds = 6.0;   // >= the deepest position (5 s, the standard IR cap) + a window

  explicit SpectralScan(double rate) : rate_(rate) {
    len_ = (size_t)(kRingSeconds * rate);
    st_ = pffft_new_setup(kN, PFFFT_REAL);
    buf_ = (float*)pffft_aligned_malloc(kN * sizeof(float));
    work_ = (float*)pffft_aligned_malloc(kN * sizeof(float));
    for (int c = 0; c < 2; c++) {
      ring_[c].assign(len_, 0.0f); ola_[c].assign(kN, 0.0f); next_[c].assign(kN, 0.0f);
      mag_[c].assign(kBins, 0.0f); tmp_[c].assign(kBins, 0.0f); pit_[c].assign(kBins, 0.0f);
    }
    out_.assign(kBins, 0.0f); pre_.assign(kBins + 1, 0.0);
    shf_ = 5000.0 * kN / rate;                           // shimmer comes from below 5 kHz (its octave up stays under ~10)
    win_.resize(kN);
    for (int i = 0; i < kN; i++) win_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / kN));
    cos_.resize(kTable); sin_.resize(kTable);
    for (int i = 0; i < kTable; i++) { cos_[i] = (float)std::cos(2 * M_PI * i / kTable); sin_[i] = (float)std::sin(2 * M_PI * i / kTable); }
    for (int b = 0; b <= kBands; b++)                                   // log bands for WIND's drift, 60 Hz .. Nyquist
      edge_[b] = std::min(kBins, std::max(1, (int)std::lround(60.0 * std::pow(rate * 0.5 / 60.0, (double)b / kBands) * kN / rate)));
    for (int b = 0; b < kBands; b++) { drift_[b] = 0; dgo_[b] = 0; }
  }
  ~SpectralScan() { pffft_aligned_free(buf_); pffft_aligned_free(work_); pffft_destroy_setup(st_); }
  SpectralScan(const SpectralScan&) = delete;
  SpectralScan& operator=(const SpectralScan&) = delete;

  void write(const float* l, const float* r, int n) {     // every block (cheap): what's heard, for when SCAN comes on
    for (int i = 0; i < n; i++) { ring_[0][w_] = l[i]; ring_[1][w_] = r[i]; if (++w_ == len_) w_ = 0; }
  }
  // n samples (n divides kHop) of the pad added to out, scaled by gain[i]
  void render(float* out_l, float* out_r, const float* gain, int n, float position, float texture, double depth,
              float pitch = 0.0f) {
    {                                                   // PITCH glides (~50 ms): moving it sounds smooth
      const double want = std::min(24.0f, std::max(-24.0f, pitch));
      if (!pr_set_) { pr_ = want; pr_set_ = true; }
      pr_ += (want - pr_) * (1.0 - std::exp(-n / (0.05 * rate_)));
      ratio_ = std::exp2(pr_ / 12.0);
    }
    tex_ = std::min(1.0f, std::max(0.0f, texture));
    pos_ = std::min(1.0f, std::max(0.0f, position));
    depth_ = std::min(kRingSeconds - 0.3, std::max(0.1, depth));
    if (!running_) start();
    const int step = hp_ / 128;                       // the next hop's frame, spread over this hop's first blocks
    if (hp_ % 128 == 0 && step < 4) { if (step < 2) analyse(step); else synth(step - 2); }
    for (int i = 0; i < n; i++) {
      if (hp_ == 0)
        for (int c = 0; c < 2; c++) {                  // (the frame goes in from the read point, wrapping once)
          float* o = ola_[c].data(); const float* f = next_[c].data(); const int a1 = kN - rp_;
          for (int k = 0; k < a1; k++) o[rp_ + k] += f[k];
          for (int k = a1; k < kN; k++) o[k - a1] += f[k];
        }
      out_l[i] += gain[i] * ola_[0][rp_]; out_r[i] += gain[i] * ola_[1][rp_];
      ola_[0][rp_] = 0.0f; ola_[1][rp_] = 0.0f;
      if (++rp_ == kN) rp_ = 0;
      if (++hp_ == kHop) hp_ = 0;                      // (the engine's blocks are 128: the work above runs at a block's start)
    }
  }
  void stop() { running_ = false; }                    // faded out: the next start begins afresh

 private:
  double uni() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) * (1.0 / 16777216.0); }   // [0, 1)
  void start() {
    running_ = true;
    for (int c = 0; c < 2; c++) { std::fill(ola_[c].begin(), ola_[c].end(), 0.0f); std::fill(next_[c].begin(), next_[c].end(), 0.0f);
                                  std::fill(mag_[c].begin(), mag_[c].end(), 0.0f); lev_[c] = 0; }
    fresh_ = true;
    hp_ = 0;                                           // (the first frame is made over the next 4 blocks, one FFT
                                                       //  each: no spike; the first hop, 46 ms, is silent under the fade)
  }
  double smoothing() const {                           // per hop: how much of the last spectrum stays
    return tex_ < 1.0 / 3 ? 0.35 + 0.6 * (tex_ * 3) : tex_ < 2.0 / 3 ? 0.95 : 0.95 + 0.03 * (tex_ - 2.0 / 3) * 3;
  }
  void analyse(int c) {                                // the window at the position, into the smoothed magnitudes
    const double back = 0.02 * rate_ + pos_ * depth_ * rate_ + kN;   // its start, behind the write head
    size_t s = (size_t)((double)w_ + (double)len_ - std::min(back, (double)len_ - 1)) % len_;
    double e = 0;
    for (int i = 0; i < kN; i++) { const float x = ring_[c][(s + i) % len_]; e += (double)x * x; buf_[i] = x * win_[i]; }
    pffft_transform_ordered(st_, buf_, buf_, work_, PFFFT_FORWARD);
    const double a = fresh_ ? 0.0 : smoothing();
    for (int k = 1; k < kBins; k++) {
      const float m = std::sqrt(buf_[2 * k] * buf_[2 * k] + buf_[2 * k + 1] * buf_[2 * k + 1]);
      mag_[c][k] = (float)(a * mag_[c][k] + (1 - a) * m);
    }
    lev_[c] = a * lev_[c] + (1 - a) * std::sqrt(e / kN);
    if (c == 1) fresh_ = false;
  }
  void synth(int c) {                                  // TEXTURE on the held spectrum, random phases, at the held level
    std::vector<float>& m = pit_[c];                   // PITCH: the held spectrum moved up or down (bins read at
    const std::vector<float>& h = mag_[c];             //  k / ratio, interpolated; shifted up, what passes Nyquist is gone)
    if (ratio_ == 1.0) std::copy(h.begin(), h.end(), m.begin());
    else
      for (int k = 1; k < kBins; k++) {
        const double q = k / ratio_; const int i = (int)q;
        m[k] = (i < 1 || i + 1 >= kBins) ? 0.0f : (float)(h[i] + (h[i + 1] - h[i]) * (q - i));
      }
    const double t = tex_;
    const double sh = t < 1.0 / 3 ? 0.0 : t < 2.0 / 3 ? (t - 1.0 / 3) * 3 : 1.0 - 0.5 * (t - 2.0 / 3) * 3;   // shimmer
    const double wind = t < 2.0 / 3 ? 0.0 : (t - 2.0 / 3) * 3;
    auto at = [&](double k) { const int i = (int)k; if (i < 1 || i + 1 >= kBins) return 0.0f; const float f = (float)(k - i); return m[i] + (m[i + 1] - m[i]) * f; };
    std::vector<float>& o = tmp_[c];
    auto src = [&](double k) { return k < shf_ ? at(k) : (float)(at(k) * std::exp(-(k - shf_) / (0.4 * shf_))); };
    for (int k = 1; k < kBins; k++)
      o[k] = (float)(m[k] + sh * (1.2 * src(k / 2.0) + 0.8 * src(k / 1.5) + 0.35 * at(k * 2.0)));   // + 8va, + 5th, - 8va
    if (wind > 0) {                                    // blurred across frequency (a box of up to +-12% of the
      pre_[0] = 0;                                     // frequency: the lows don't smear into rumble), and drifting bands
      for (int k = 1; k <= kBins; k++) pre_[k] = pre_[k - 1] + (k < kBins ? o[k] : 0.0f);
      for (int k = 1; k < kBins; k++) {
        const int r = std::max(1, (int)(wind * 0.12 * k));
        const int lo = std::max(1, k - r), hi = std::min(kBins - 1, k + r);
        out_[k] = (float)((pre_[hi] - pre_[lo - 1]) / (hi - lo + 1));
      }
      if (c == 0)                                      // (the drift moves once a hop: ~0.3 Hz random walks per band)
        for (int b = 0; b < kBands; b++) { dgo_[b] = 0.97 * dgo_[b] + 0.03 * (uni() * 2 - 1); drift_[b] = std::max(-1.0, std::min(1.0, drift_[b] + 0.25 * dgo_[b])); }
      for (int b = 0; b < kBands; b++) {
        const float g = (float)std::exp2(wind * 1.5 * drift_[b]);   // up to +-9 dB
        for (int k = edge_[b]; k < edge_[b + 1]; k++) o[k] = (float)((1 - wind) * o[k] + wind * out_[k]) * g;
      }
    }
    buf_[0] = 0.0f; buf_[1] = 0.0f;
    for (int k = 1; k < kBins; k++) {
      rng_ = rng_ * 1664525u + 1013904223u;
      const int p = (int)(rng_ >> 20) & (kTable - 1);
      buf_[2 * k] = o[k] * cos_[p]; buf_[2 * k + 1] = o[k] * sin_[p];
    }
    pffft_transform_ordered(st_, buf_, buf_, work_, PFFFT_BACKWARD);
    double e = 0;
    for (int i = 0; i < kN; i++) e += (double)buf_[i] * buf_[i];
    const double fr = std::sqrt(e / kN);               // Hann^2 overlap-added at kN / 4: 1.5 x a frame's power
    const double g = fr > 1e-20 ? lev_[c] / (std::sqrt(1.5) * fr) : 0.0;
    for (int i = 0; i < kN; i++) next_[c][i] = (float)(buf_[i] * win_[i] * g);
  }

  double rate_;
  size_t len_ = 0, w_ = 0;
  PFFFT_Setup* st_ = nullptr;
  float* buf_ = nullptr;
  float* work_ = nullptr;
  std::vector<float> ring_[2], ola_[2], next_[2], mag_[2], tmp_[2], pit_[2], out_, win_, cos_, sin_;
  std::vector<double> pre_;
  double shf_ = 0;
  double lev_[2] = {0, 0};
  int edge_[kBands + 1];
  double drift_[kBands], dgo_[kBands];
  int rp_ = 0, hp_ = 0;
  bool running_ = false, fresh_ = true;
  double tex_ = 0, pos_ = 0, depth_ = 1, ratio_ = 1, pr_ = 0;
  bool pr_set_ = false;
  uint32_t rng_ = 0x2545F491u;
};

}  // namespace irrev
