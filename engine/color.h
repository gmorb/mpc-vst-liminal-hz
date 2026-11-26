// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// MOTION and AGE: two one-knob colours for the wet signal (after BEND), each out of the path at 0.
//  Motion: a three-voice chorus per side (short delays, 8-19 ms, swayed by slow sines, the right side a quarter turn
//          from the left), mixed in with the wet: the tail moves and widens, like a dimension / ensemble effect.
//  Age:    a tape's wear: wow (0.6 Hz) and flutter (7 Hz) on a short delay, both sides together; a soft saturation that
//          keeps quiet parts at their level and rounds the peaks; the top end closing from 18 kHz to 4.5 kHz.
// Audio thread only; no allocation after construction.
#pragma once
#include <cmath>
#include <vector>

namespace irrev {

class ColorStage {
 public:
  explicit ColorStage(double rate) : rate_(rate) {
    n_ = 1; while (n_ < (int)(0.05 * rate) + 8) n_ <<= 1;
    for (int c = 0; c < 2; c++) { mb_[c].assign(n_, 0.0f); ab_[c].assign(n_, 0.0f); }
    // the sways: phasors turned by a fixed step each sample (no sin() per sample); 0-5: Motion's voices (L, R a
    // quarter turn on), 6: Age's wow, 7: its flutter
    static const double kHz[8] = {0.23, 0.37, 0.51, 0.23, 0.37, 0.51, 0.6 / (2 * M_PI), 7.0 / (2 * M_PI)};
    static const double kPh[8] = {0.0, 2.1, 4.2, 1.5708, 3.6708, 5.7708, 0.0, 1.0};
    for (int k = 0; k < 8; k++) {
      const double w = 2 * M_PI * kHz[k] / rate;
      cr_[k] = std::cos(w); sr_[k] = std::sin(w); ps_[k] = std::sin(kPh[k]); pc_[k] = std::cos(kPh[k]);
    }
  }
  // one block of the wet signal, in place. motion, age: 0..1
  void process(float* l, float* r, int n, float motion, float age) {
    const float fade = 1.0f / (0.03f * (float)rate_);
    const bool mo = motion > 0.001f, ao = age > 0.001f;
    for (int k = 0; k < 8; k++) {                          // (once a block: the phasors stay on the unit circle)
      const double m = 1.0 / std::sqrt(ps_[k] * ps_[k] + pc_[k] * pc_[k]); ps_[k] *= m; pc_[k] *= m;
    }
    if (!mo && !ao && mg_ <= 0.0f && ag_ <= 0.0f) {          // both off: only kept current (no stale start)
      for (int i = 0; i < n; i++) { write(mb_, l[i], r[i]); write(ab_, l[i], r[i]); turn(); w_ = (w_ + 1) & (n_ - 1); }
      return;
    }
    // Age's low-pass: 18 kHz -> 4.5 kHz along the knob (exponential), one pole
    const float hz = 18000.0f * std::pow(0.25f, age);
    const float lpk = 1.0f - std::exp(-2.0f * 3.14159265f * hz / (float)rate_);
    const float drive = 1.0f + 3.0f * age;
    static const double kBase[3] = {0.008, 0.013, 0.019};
    const double depth = 0.0035 * motion * rate_;
    for (int i = 0; i < n; i++) {
      write(mb_, l[i], r[i]); write(ab_, l[i], r[i]);
      mg_ = mo ? std::min(1.0f, mg_ + fade) : std::max(0.0f, mg_ - fade);
      ag_ = ao ? std::min(1.0f, ag_ + fade) : std::max(0.0f, ag_ - fade);
      float x[2] = {l[i], r[i]};
      if (mg_ > 0.0f) {                                    // MOTION
        // equal power: the voices are unrelated to the wet (and to each other) but at the lowest frequencies
        const float wmix = 0.6f * motion * mg_, keep = std::sqrt(1.0f - wmix * wmix);
        for (int c = 0; c < 2; c++) {
          float s = 0;
          for (int v = 0; v < 3; v++) s += read(mb_[c], kBase[v] * rate_ + depth * ps_[3 * c + v]);
          x[c] = keep * x[c] + wmix * 0.57735f * s;
        }
      }
      if (ag_ > 0.0f) {                                    // AGE
        const double d = (0.003 + age * (0.0006 * ps_[6] + 0.00008 * ps_[7])) * rate_;
        for (int c = 0; c < 2; c++) {
          const float y = soft(drive * read(ab_[c], d)) / drive;   // quiet parts as they were, peaks rounded
          lp_[c] += lpk * (y - lp_[c]);
          x[c] += ag_ * (lp_[c] - x[c]);
        }
      }
      l[i] = x[0]; r[i] = x[1];
      turn();
      w_ = (w_ + 1) & (n_ - 1);
    }
  }

 private:
  static float soft(float x) {                             // tanh, near enough (Pade 3/2), flat at +-1 beyond 3
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }
  void turn() {
    for (int k = 0; k < 8; k++) {
      const double s = ps_[k] * cr_[k] + pc_[k] * sr_[k], c = pc_[k] * cr_[k] - ps_[k] * sr_[k];
      ps_[k] = s; pc_[k] = c;
    }
  }
  void write(std::vector<float>* b, float a, float c) { b[0][w_] = a; b[1][w_] = c; }
  float read(const std::vector<float>& b, double d) const {   // d samples behind the sample just written
    const double rp = (double)w_ - d;
    const int i0 = (int)std::floor(rp);
    const float f = (float)(rp - i0);
    const float a = b[(i0 + n_) & (n_ - 1)], c = b[(i0 + 1 + n_) & (n_ - 1)];
    return a + (c - a) * f;
  }
  double rate_;
  int n_ = 0, w_ = 0;
  std::vector<float> mb_[2], ab_[2];
  float mg_ = 0, ag_ = 0, lp_[2] = {0, 0};
  double cr_[8], sr_[8], ps_[8], pc_[8];
};

}  // namespace irrev
