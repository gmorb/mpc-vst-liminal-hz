// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* grains.h -- SCAN's GRAINS mode: a granulator over the reverb's last few seconds (mpc-vst-liminal-hz).
 *
 * The first version (demo 1) placed grains at random: their waveforms cancelled and came back, so the level jumped
 * by 25-35 dB on a steady tone: heard as jitter, cracks and pops. Here:
 *   - each new grain's start is moved (within +-kTolMs) to where the source lines up best with what the last grain
 *     is playing now (normalised cross-correlation, coarse then fine: WSOLA), so grains join without cancelling;
 *   - grains are evenly spaced (irregular only when sparse: droplets);
 *   - the sum is divided by the sum of the windows playing (when they overlap), and a slow level match (~200 ms)
 *     keeps it at the level of what the grains read: steady, whatever Size and Density.
 * Grains of kMinMs..kMaxMs with a Hann window; 4-point (Catmull-Rom) reads; pitch as a ratio in semitones, the same
 * for every grain; a grain shifted up is low-passed under the new Nyquist.
 *   position  where grains are read: 0 = just behind now, 1 = `depth` seconds back (the IR's length: the glow)
 *   size      0..1 -> kMinMs..kMaxMs (on a log curve)
 *   pitch     semitones, -24..+24, any value (the plugin may snap it); it glides (~50 ms), and grains already playing
 *             follow it, so moving it sounds smooth, not stepped
 *   spray     0..1: how far each grain lands from the position (+-0 .. +-1 s)
 *   density   0..1: grains overlapping, 0.5 (droplets, with gaps) .. 6 (a smooth cloud)
 *   air       0..1: AIR, the lush part, all from one knob: each grain detuned at random (up to +-18 cents: a chorus),
 *             spread wider in stereo; from 25% some grains play backwards (to 30% of them), from 40% some an octave
 *             up (to 30%); the cloud's lows thinned (a high-pass rising to 250 Hz) and its top lifted (to +4 dB above
 *             ~3.5 kHz); above 50% the cloud feeds back into what the grains read (to 0.6, soft-clipped): a cloud that
 *             builds and, with the octave grains, shimmers. 0 = exactly the cloud without it.
 *   beats     SYNC: > 0 = a grain every `beats` (quarter notes) at the host's tempo, on its grid while the transport
 *             plays (from its song position, ppq: on the beat, through loops and jumps), at that rate while stopped;
 *             evenly, no jitter. 0 = free (Density sets the rate).
 * Audio thread, allocation-free after the constructor.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace irrev {

class GrainCloud {
 public:
  static constexpr double kRingSeconds = 6.0, kMinMs = 30.0, kMaxMs = 500.0, kTolMs = 9.0;
  static constexpr int kVoices = 24, kWin = 1024, kCorr = 256;

  explicit GrainCloud(double rate) : rate_(rate) {
    len_ = (size_t)(kRingSeconds * rate);
    ring_[0].assign(len_, 0.0f); ring_[1].assign(len_, 0.0f);
    for (int i = 0; i <= kWin; i++) hann_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / kWin));
    tol_ = (int)(kTolMs * 0.001 * rate);
  }
  void write(const float* l, const float* r, int n) {
    for (int i = 0; i < n; i++) { ring_[0][w_] = l[i]; ring_[1][w_] = r[i]; if (++w_ == len_) w_ = 0; }
  }
  static double size_ms(double size) { return kMinMs * std::pow(kMaxMs / kMinMs, clamp01(size)); }
  static double overlap_of(double density) { return 0.5 * std::pow(6.0 / 0.5, clamp01(density)); }

  void render(float* out_l, float* out_r, const float* gain, int n, float position, float size, float pitch,
              float spray, float density, double depth, float air = 0.0f, double beats = 0.0,
              double ppq = 0.0, double bpm = 120.0, bool playing = false) {
    const double pps = bpm / 60.0 / rate_;                              // SYNC: quarter notes per sample
    air_ = clamp01(air);
    const double hpc = 1.0 - std::exp(-2 * M_PI * (30.0 + 220.0 * air_) / rate_), shc = 1.0 - std::exp(-2 * M_PI * 3500.0 / rate_);
    const double lift = 0.6 * air_, fb = air_ > 0.5 ? 0.6 * (air_ - 0.5) / 0.5 : 0.0;
    const double glen = size_ms(size) * 0.001 * rate_;
    const double ov = overlap_of(density);
    const double interval = glen / ov;
    const double want = std::min(24.0f, std::max(-24.0f, pitch));
    if (!pr_set_) { pr_ = want; pr_set_ = true; }
    pr_ += (want - pr_) * (1.0 - std::exp(-n / (0.05 * rate_)));       // the glide, per block
    const double ratio = std::exp2(pr_ / 12.0);
    for (auto& g : v_)                                                  // grains playing follow it
      if (g.on && g.mult * ratio != std::fabs(g.step)) {
        const double rg = g.mult * ratio;
        g.step = g.step < 0 ? -rg : rg;
        g.lp = rg > 1.0 ? 1.0 - std::exp(-2 * M_PI * std::min(0.45 * rate_ / rg, 0.45 * rate_) / rate_) : 0.0;
      }
    depth = std::min(kRingSeconds - 2.0, std::max(0.1, depth));
    for (int i = 0; i < n; i++) {
      if (beats > 0 && playing) {                                       // SYNC, playing: on the host's grid
        const long k = (long)std::floor((ppq + i * pps) / beats + 1e-9);
        if (k != grid_k_) {
          // (a loop's wrap: the block before it ran past the loop's end and already fired that boundary, so a step
          //  back that lands within half a step of the last grain is the same moment, not a new one)
          const bool same = k < grid_k_ && since_ < 0.5 * beats / pps;
          if (grid_set_ && !same) { spawn(position, glen, ratio, spray, depth); spawned_++; since_ = 0; }
          grid_k_ = k; grid_set_ = true;
        }
        since_++;
        next_ = 0;
      } else if (beats > 0) {                                           // SYNC, stopped: at the tempo's rate
        grid_set_ = false;
        if (--next_ <= 0) { spawn(position, glen, ratio, spray, depth); spawned_++; next_ += std::max(1.0, beats / pps); }
      } else if (--next_ <= 0) {
        grid_set_ = false;
        spawn(position, glen, ratio, spray, depth); spawned_++;
        const double jit = ov < 1.0 ? 0.35 * (1.0 - ov) : 0.03;      // even, except as droplets
        next_ += std::max(1.0, interval * (1.0 - jit + 2.0 * jit * uni()));
      }
      double sl = 0, sr = 0, ws = 0, ps = 0;
      for (auto& g : v_) {
        if (!g.on) continue;
        const double ph = g.t * g.inv * kWin;
        const int k = (int)ph;
        const double win = hann_[k] + (hann_[k + 1] - hann_[k]) * (ph - k);
        double xl = read(0, g.pos), xr = read(1, g.pos);
        if (g.lp > 0) {                                  // (shifted up: two one-poles under the new Nyquist)
          g.l1 += g.lp * (xl - g.l1); g.l2 += g.lp * (g.l1 - g.l2); xl = g.l2;
          g.r1 += g.lp * (xr - g.r1); g.r2 += g.lp * (g.r1 - g.r2); xr = g.r2;
        }
        const double ml = xl * g.gl + xr * g.xl, mr = xr * g.gr + xl * g.xr;
        sl += win * ml; sr += win * mr; ws += win; ps += win * (ml * ml + mr * mr);
        g.lev += 0.004 * (win * win * (ml * ml + mr * mr) - g.lev);   // (how loud it plays, ~5 ms: for the display)
        g.pos += g.step;
        if (g.pos >= len_) g.pos -= len_;
        else if (g.pos < 0) g.pos += len_;
        if (++g.t >= g.len) g.on = false;
      }
      const double d = 1.0 / std::max(1.0, ws);         // overlapping: the windows' sum (droplets keep their fades)
      sl *= d; sr *= d;
      if (air_ > 0) {                                    // AIR: thinner lows, a lifted top (before the level match)
        hl_ += hpc * (sl - hl_); hr_ += hpc * (sr - hr_); sl -= hl_; sr -= hr_;
        tl_ += shc * (sl - tl_); tr_ += shc * (sr - tr_); sl += lift * (sl - tl_); sr += lift * (sr - tr_);
      }
      // the level of what's read vs. what comes out (~200 ms): grains of a reverb only partly add up in phase
      const double a = 1.0 / (0.2 * rate_);
      src_ += a * ((ws > 1e-6 ? ps / ws : 0.0) * std::min(1.0, ws) - src_);
      out_ += a * (sl * sl + sr * sr - out_);
      const double want = out_ > 1e-12 ? std::sqrt(src_ / out_) : 1.0;
      agc_ += 0.0005 * (std::min(2.5, std::max(0.5, want)) - agc_);
      out_l[i] += (float)(gain[i] * sl * agc_);
      out_r[i] += (float)(gain[i] * sr * agc_);
      if (fb > 0) {                                      // AIR's feedback: the cloud into what it reads (this block's
        const size_t j = wrap((long)w_ - n + i);         //  input, just written)
        ring_[0][j] += (float)std::tanh(fb * gain[i] * sl * agc_); ring_[1][j] += (float)std::tanh(fb * gain[i] * sr * agc_);
      }
    }
  }
  void reset() { for (auto& g : v_) g.on = false; next_ = 0; last_ = -1; src_ = out_ = 0; agc_ = 1; hl_ = hr_ = tl_ = tr_ = 0; pr_set_ = false; grid_set_ = false; }
  long spawned() const { return spawned_; }             // grains started (tests)
  // one stripe per column, for the display: the loudest grain reading in it (heard: over -60 dB), at its place inside
  // the column (1..npos, 0 = none). Each stripe is one real grain where it reads now, so it stands still at PITCH 0
  // (it reads at the speed the sound arrives) and drifts when pitched (it reads faster or slower than that).
  void stripes(int* out, int ncols, int npos, double depth) const {
    depth = std::min(kRingSeconds - 2.0, std::max(0.1, depth));
    double best[64] = {};
    for (int c = 0; c < ncols; c++) out[c] = 0;
    for (const auto& g : v_) {
      if (!g.on || g.lev < 1e-6) continue;
      const double back = (double)((long)w_ - (long)g.pos + (g.pos > w_ ? (long)len_ : 0)) / rate_;
      const double x = std::min(ncols - 1e-6, std::max(0.0, (back - 0.03) / depth * ncols));
      const int c = (int)x;
      if (g.lev > best[c]) { best[c] = g.lev; out[c] = 1 + std::min(npos - 1, (int)((x - c) * npos)); }
    }
  }
  // where the grains playing read, for the display: each grain added to its column (0 = just behind now, ncols - 1 =
  // `depth` back, as Position), weighted by how loud it plays (nothing under -70 dB, full from -40 dB: silent grains
  // don't show), into cols[0..ncols)
  void activity(float* cols, int ncols, double depth) const {
    depth = std::min(kRingSeconds - 2.0, std::max(0.1, depth));
    for (const auto& g : v_) {
      if (!g.on) continue;
      const double back = (double)((long)w_ - (long)g.pos + (g.pos > w_ ? (long)len_ : 0)) / rate_;
      const double db = 10 * std::log10(g.lev + 1e-12);
      const float wgt = (float)std::min(1.0, std::max(0.0, (db + 70.0) / 30.0));
      // shared between the two nearest columns by where it is (column centres at c + 0.5): moving, it glides across
      const double x = std::min((double)ncols - 0.5, std::max(0.5, (back - 0.03) / depth * ncols)) - 0.5;
      const int c = std::min(ncols - 2, (int)x);
      const float f = (float)(x - c);
      cols[c] += wgt * (1.0f - f); cols[c + 1] += wgt * f;
    }
  }

 private:
  struct Grain {
    bool on = false;
    double lev = 0, mult = 1, pos = 0, step = 1, t = 0, len = 1, inv = 1, gl = 1, gr = 1, xl = 0, xr = 0, lp = 0, l1 = 0, l2 = 0, r1 = 0, r2 = 0;
  };
  static double clamp01(double x) { return std::min(1.0, std::max(0.0, x)); }
  double uni() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) * (1.0 / 16777216.0); }
  size_t wrap(long i) const { long m = (long)len_; i %= m; return (size_t)(i < 0 ? i + m : i); }
  float mono(long i) const { const size_t j = wrap(i); return ring_[0][j] + ring_[1][j]; }
  double read(int c, double at) const {                  // 4-point Catmull-Rom
    const size_t i1 = (size_t)at % len_, i0 = (i1 + len_ - 1) % len_, i2 = (i1 + 1) % len_, i3 = (i1 + 2) % len_;
    const double f = at - std::floor(at);
    const double y0 = ring_[c][i0], y1 = ring_[c][i1], y2 = ring_[c][i2], y3 = ring_[c][i3];
    return y1 + 0.5 * f * (y2 - y0 + f * (2 * y0 - 5 * y1 + 4 * y2 - y3 + f * (3 * (y1 - y2) + y3 - y0)));
  }
  // the start near `s` whose next kCorr samples (at the grain's speed) best match those from `ref`
  long align(long s, long ref, double ratio) {
    const int st = std::max(1, (int)std::lround(ratio));   // compare in the source, at the speed both are read
    float r[kCorr];
    double rr = 0;
    for (int j = 0; j < kCorr; j++) { r[j] = mono(ref + (long)j * st); rr += (double)r[j] * r[j]; }
    if (rr < 1e-12) return s;
    auto score = [&](long c, int every) {
      double xy = 0, xx = 0;
      for (int j = 0; j < kCorr; j += every) { const float x = mono(c + (long)j * st); xy += x * r[j]; xx += (double)x * x; }
      return xx > 1e-12 ? xy / std::sqrt(xx) : -1e30;
    };
    long best = s; double bs = -1e30;
    for (long c = s - tol_; c <= s + tol_; c += 4) { const double v = score(c, 4); if (v > bs) { bs = v; best = c; } }
    const long mid = best;
    bs = -1e30;
    for (long c = mid - 3; c <= mid + 3; c++) { const double v = score(c, 1); if (v > bs) { bs = v; best = c; } }
    return best;
  }
  void spawn(float position, double glen, double ratio, float spray, double depth) {
    int gi = -1;
    for (int x = 0; x < kVoices; x++) if (!v_[x].on) { gi = x; break; }
    if (gi < 0) return;
    Grain* g = &v_[gi];
    const double len = std::max(64.0, std::floor(glen));
    const double sp = clamp01(spray);
    const double reach = sp * sp * rate_;                              // +- this around the position
    // AIR: this grain's own pitch (detuned; maybe an octave up) and direction
    const double det = air_ > 0 ? std::exp2((uni() * 2 - 1) * 18.0 * air_ / 1200.0) : 1.0;
    const bool oct = air_ > 0.4 && ratio <= 2.0 && uni() < 0.3 * (air_ - 0.4) / 0.6;
    const bool rev = air_ > 0.25 && uni() < 0.3 * (air_ - 0.25) / 0.75;
    const double rg = ratio * det * (oct ? 2.0 : 1.0);
    const double tail = len * rg + tol_ + 2 * kCorr * std::max(1.0, rg);
    double back = (0.03 + clamp01(position) * depth) * rate_ + (uni() * 2 - 1) * reach;
    back = std::min(std::max(back, 0.03 * rate_ + tail), (double)len_ - 2.0 * tol_ - 8.0);   // clear of the write head
    long s = (long)w_ - (long)back;
    if (!oct && !rev && last_ >= 0 && v_[last_].on) s = align(s, (long)v_[last_].pos, ratio);   // join the grain playing
    double start = (double)wrap(rev ? s + (long)(len * rg) : s);       // (backwards: from its end)
    g->on = true; g->lev = 0; g->t = 0; g->len = len; g->inv = 1.0 / len; g->pos = start; g->step = rev ? -rg : rg; g->mult = rg / ratio;
    const double fc = 0.45 * rate_ / rg;
    g->lp = rg > 1.0 ? 1.0 - std::exp(-2 * M_PI * std::min(fc, 0.45 * rate_) / rate_) : 0.0;
    g->l1 = g->l2 = g->r1 = g->r2 = 0.0;
    if (g->lp > 0) { g->l1 = g->l2 = read(0, start); g->r1 = g->r2 = read(1, start); }
    // stereo: the reverb's own sides, each grain leaning a little left or right (more with Spray)
    const double pan = (uni() * 2 - 1) * std::min(1.0, 0.15 + 0.45 * sp + 0.7 * air_);
    g->gl = std::sqrt(1 - pan); g->gr = std::sqrt(1 + pan);
    g->xl = std::max(0.0, -pan) * 0.5; g->xr = std::max(0.0, pan) * 0.5;
    if (!oct && !rev) last_ = gi;                       // (the next grain joins a plain one)
  }

  double rate_;
  size_t len_ = 0, w_ = 0;
  int tol_ = 0, last_ = -1;
  std::vector<float> ring_[2];
  Grain v_[kVoices];
  float hann_[kWin + 1];
  double pr_ = 0;
  bool pr_set_ = false, grid_set_ = false;
  long grid_k_ = 0, spawned_ = 0;
  double since_ = 1e12;
  double next_ = 0, src_ = 0, out_ = 0, agc_ = 1, air_ = 0, hl_ = 0, hr_ = 0, tl_ = 0, tr_ = 0;
  uint32_t rng_ = 0x6C8E9CF5u;
};

}  // namespace irrev
