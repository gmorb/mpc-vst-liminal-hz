// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// PitchShifter: the classic delay-line pitch shifter (two taps sliding through a short delay line, each faded in and
// out under a raised-cosine window, half a window apart, so one is always heard while the other jumps back). The taps
// slide at (1 - ratio) samples per sample: slower than the writer is lower, faster is higher. The ratio glides, so a
// moving knob bends what's playing smoothly, with no steps and no added latency beyond the taps' short delay. On a
// diffuse signal such as a reverb tail the taps' seams are hidden; on a dry note they would warble a little.
// Used for BEND (the wet signal, -12..+12 semitones) and SHIMMER (an octave up, fed back into the IR).
// Audio thread only; no allocation after construction.
#pragma once
#include <cmath>
#include <vector>

namespace irrev {

class PitchShifter {
 public:
  // window: the taps' window in seconds (longer: smoother on pads, more smear on hits)
  PitchShifter(double rate, double window) {
    win_ = std::max(64, (int)std::lround(window * rate));
    n_ = 1; while (n_ < win_ * 2 + 8) n_ <<= 1;
    buf_.assign(n_, 0.0f);
    win_tab_.resize(kTab + 1);
    for (int i = 0; i <= kTab; i++) win_tab_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / kTab));   // 0 -> 1 -> 0
  }
  void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); ph_ = 0; }
  // one sample in, one out, at ratio (2^(semitones/12))
  float run(float x, float ratio) {
    buf_[w_] = x;
    ph_ += (1.0 - ratio) / win_;                 // the taps' sweep through the window (0..1)
    ph_ -= std::floor(ph_);
    const double p2 = ph_ + 0.5 - (ph_ >= 0.5 ? 1.0 : 0.0);
    const float y = tap(ph_) + tap(p2);
    w_ = (w_ + 1) & (n_ - 1);
    return y;
  }
  int window() const { return win_; }

 private:
  static constexpr int kTab = 1024;
  float tap(double p) const {                    // the tap at phase p: delay 2 + p * window, weighted by the window
    const double d = 2.0 + p * win_;
    const double rp = (double)w_ - d;
    const int i0 = (int)std::floor(rp);
    const float f = (float)(rp - i0);
    const float a = buf_[(i0 + n_) & (n_ - 1)], b = buf_[(i0 + 1 + n_) & (n_ - 1)];
    const float g = win_tab_[(int)(p * kTab)];
    return g * (a + (b - a) * f);
  }
  std::vector<float> buf_, win_tab_;
  int n_ = 0, w_ = 0, win_ = 0;
  double ph_ = 0;
};

}  // namespace irrev
