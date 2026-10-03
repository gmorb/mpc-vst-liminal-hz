// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "reverb_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Resample.h"
#include "wav_reader.h"

namespace irrev {

// flush-to-zero while processing: a decaying reverb tail must not fall into denormal arithmetic (slow on ARM)
struct FlushDenormals {
#if defined(__arm__)
  uint32_t old;
  FlushDenormals() { __asm__ __volatile__("vmrs %0, fpscr" : "=r"(old)); uint32_t v = old | (1u << 24);
                     __asm__ __volatile__("vmsr fpscr, %0" : : "r"(v)); }
  ~FlushDenormals() { __asm__ __volatile__("vmsr fpscr, %0" : : "r"(old)); }
#elif defined(__SSE__) || defined(__x86_64__)
  unsigned old;
  FlushDenormals() { old = __builtin_ia32_stmxcsr(); __builtin_ia32_ldmxcsr(old | 0x8040); }
  ~FlushDenormals() { __builtin_ia32_ldmxcsr(old); }
#else
  FlushDenormals() {}
#endif
};

// ------------------------------------------------------------------------------------------------ ReverbIR
// a one-line description of how the shaped IR sounds: decay (RT60 from the Schroeder curve, or "swells"), colour,
// whether it darkens or brightens, stereo, and echoes vs diffuse. Off the audio thread (IR prep).
static std::string describe(const std::vector<std::vector<float>>& h, double rate, bool reversed, ReverbIR* ir) {
  const size_t n = h[0].size();
  if (n < 64) return "";
  std::vector<double> en(n, 0.0), hf(n, 0.0);
  for (auto& v : h)
    for (size_t i = 0; i < n; i++) { en[i] += (double)v[i] * v[i]; if (i) { const double d = (double)v[i] - v[i - 1]; hf[i] += d * d; } }
  std::vector<double> sch(n);                                // Schroeder backward integration
  double acc = 0;
  for (size_t i = n; i-- > 0;) { acc += en[i]; sch[i] = acc; }
  const double total = sch[0];
  std::string out;
  char buf[48];
  size_t peak_i = 0;
  for (size_t i = 0; i < n; i++) if (en[i] > en[peak_i]) peak_i = i;
  if (reversed || peak_i > n / 2) {
    out = "swells";
    ir->swell = (float)((double)peak_i / rate);
  }
  else {
    auto t_at = [&](double db) { for (size_t i = 0; i < n; i++) if (10 * std::log10(sch[i] / total + 1e-30) <= db) return (double)i / rate; return (double)n / rate; };
    const double t5 = t_at(-5), t25 = t_at(-25), t15 = t_at(-15);
    const double rt = t25 < (double)n / rate ? 3.0 * (t25 - t5) : 6.0 * (t15 - t5);
    const double len = (double)n / rate;                    // an estimate past the IR's own end (echo patterns
    if (rt > len) {                                          // extrapolate badly): say "at least its length"
      ir->t60 = (float)len;
      snprintf(buf, sizeof buf, "decay %.1f s+", len);
    } else {
      ir->t60 = (float)std::max(0.05, rt);
      snprintf(buf, sizeof buf, "decay %.1f s", std::max(0.05, rt));
    }
    out = buf;
  }
  auto ratio = [&](size_t a, size_t b) { double e = 0, d = 0; for (size_t i = a; i < b && i < n; i++) { e += en[i]; d += hf[i]; } return e > 1e-20 ? d / e : 0.0; };
  const double all = ratio(0, n), early = ratio(0, n / 5), late = ratio(n * 2 / 5, n * 4 / 5);
  ir->bright = (float)all;
  // (white noise reads 2.0 on this scale; a reverb's colour is mostly its first, loudest part)
  out += all < 0.25 ? " \xC2\xB7 dark" : all < 0.6 ? " \xC2\xB7 warm" : all < 1.2 ? " \xC2\xB7 bright" : " \xC2\xB7 airy";
  if (!reversed && early > 0 && late > 0) {
    if (late < early * 0.6) out += " \xC2\xB7 darkens";
    else if (late > early * 1.4) out += " \xC2\xB7 brightens";
  }
  if (h.size() < 2) out += " \xC2\xB7 mono";
  else {
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 0; i < n; i++) { lr += (double)h[0][i] * h[1][i]; ll += (double)h[0][i] * h[0][i]; rr += (double)h[1][i] * h[1][i]; }
    const double c = lr / std::sqrt(ll * rr + 1e-30);
    ir->width = (float)std::min(1.0, std::max(0.0, 1.0 - c));
    out += c < 0.3 ? " \xC2\xB7 wide" : c < 0.75 ? " \xC2\xB7 stereo" : " \xC2\xB7 narrow";
  }
  const size_t win = (size_t)(0.01 * rate);                  // echoes: 10 ms windows well above their neighbourhood
  if (win > 0 && n > win * 40) {
    std::vector<double> w;
    for (size_t i = 0; i + win <= n; i += win) { double s = 0; for (size_t j = i; j < i + win; j++) s += en[j]; w.push_back(s); }
    int jumps = 0;
    for (size_t k = 4; k + 4 < w.size(); k++) {
      double around = 0; for (size_t j = k - 4; j <= k + 4; j++) if (j != k) around += w[j];
      around /= 8;
      if (w[k] > 4.0 * around && w[k] > w[k - 1] && w[k] >= w[k + 1] && w[k] > total * 1e-4) jumps++;
    }
    out += jumps >= 3 ? " \xC2\xB7 echoes" : " \xC2\xB7 diffuse";
  }
  return out;
}

std::unique_ptr<ReverbIR> ReverbIR::make(const std::vector<std::vector<float>>& channels, double rate, double host_rate,
                                         const IRShape& shape, std::string* err, LongConvolver::Mode mode) {
  if (channels.empty() || channels[0].empty() || rate <= 0 || host_rate <= 0) { if (err) *err = "Empty IR"; return nullptr; }
  const size_t nch = std::min<size_t>(2, channels.size());
  const double stretch = std::min(2.0, std::max(0.5, shape.stretch));
  const double target = host_rate * stretch;               // stretch: resampled for a longer (lower) or shorter IR
  std::vector<std::vector<float>> h(nch);
  for (size_t c = 0; c < nch; c++) {                       // to the host rate: cubic, zero-padded (as NAM's cab IRs)
    if (rate == target) { h[c] = channels[c]; continue; }
    std::vector<float> padded(channels[c].size() + 2, 0.0f);
    memcpy(padded.data() + 1, channels[c].data(), channels[c].size() * sizeof(float));
    dsp::ResampleCubic<float>(padded, rate, target, 0.0, h[c]);
  }
  size_t n = h[0].size();
  for (auto& v : h) n = std::min(n, v.size());
  n = std::min(n, (size_t)(kMaxSeconds * host_rate));     // the 5 s cap (after stretch: it's about CPU)
  for (auto& v : h) v.resize(n, 0.0f);
  if (shape.reverse) for (auto& v : h) std::reverse(v.begin(), v.end());
  // trim: [a, b) of the capped IR, at least 128 samples
  size_t a = (size_t)std::llround(n * std::min(0.9, std::max(0.0, shape.start)));
  size_t b = (size_t)std::llround(n * std::min(1.0, std::max(0.02, shape.length)));
  if (b > n) b = n;
  if (b < a + 128) b = std::min(n, a + 128);
  if (b < a + 128) a = b > 128 ? b - 128 : 0;
  const size_t len = b - a;
  for (auto& v : h) v = std::vector<float>(v.begin() + a, v.begin() + b);
  n = len;
  const size_t fin = (size_t)(len * std::min(0.5, std::max(0.0, shape.fade_in)));
  const size_t fout = std::max<size_t>(32, (size_t)(len * std::min(0.5, std::max(0.0, shape.fade_out))));
  for (auto& v : h) {                                      // raised-cosine fades (no clicks at either end)
    for (size_t i = 0; i < fin && i < n; i++) v[i] *= (float)(0.5 * (1.0 - std::cos(M_PI * (i + 1) / (fin + 1))));
    for (size_t i = 0; i < fout && i < n; i++) v[n - fout + i] *= (float)(0.5 * (1.0 + std::cos(M_PI * (i + 1) / fout)));
  }
  double e = 0;                                            // unit energy: IRs come out at similar levels
  for (auto& v : h) for (float x : v) e += (double)x * x;
  e /= (double)nch;
  if (!(e > 1e-20)) { if (err) *err = "Silent IR"; return nullptr; }
  const float g = (float)(1.0 / std::sqrt(e));
  for (auto& v : h) for (float& x : v) x *= g;
  std::unique_ptr<ReverbIR> ir(new ReverbIR());
  {                                                         // what the page shows: per column, level and tone
    double top = 0;
    std::array<double, kCols> pk{}, e{}, ed{};
    for (int c = 0; c < kCols; c++) {
      const size_t lo = n * c / kCols, hi = std::max(lo + 1, n * (c + 1) / kCols);
      for (auto& v : h)
        for (size_t i = lo; i < hi && i < n; i++) {
          pk[c] = std::max(pk[c], (double)std::fabs(v[i]));
          e[c] += (double)v[i] * v[i];
          if (i > 0) { const double d = (double)v[i] - v[i - 1]; ed[c] += d * d; }   // high-frequency energy
        }
      top = std::max(top, pk[c]);
    }
    for (int c = 0; c < kCols; c++) {
      const double db = pk[c] > 0 && top > 0 ? 20 * std::log10(pk[c] / top) : -1e9;
      // 66 dB: a reverb tail stays audible to about -60 dB (48 dB left half the display empty on most spaces)
      const int level = (int)std::min(7.0, std::max(0.0, std::round((db + 66.0) / 66.0 * 7.0)));
      // white noise: ed/e = 2 (at any rate); a dark tail far less. 0.22 / 0.75: dark / balanced / airy
      const double hf = e[c] > 1e-20 ? ed[c] / e[c] : 0;
      const int tone = hf < 0.22 ? 0 : hf < 0.75 ? 1 : 2;
      ir->wave[c] = (uint8_t)(level == 0 ? 0 : tone * 8 + level);
      ir->tint[c] = (uint8_t)tone;
    }
    ir->sonics = describe(h, host_rate, shape.reverse, ir.get());
  }
  ir->stereo = nch == 2;
  ir->frames = n;
  ir->l.reset(new LongConvolver(h[0], mode));
  ir->r.reset(new LongConvolver(h[nch == 2 ? 1 : 0], mode));
  return ir;
}

std::unique_ptr<ReverbIR> ReverbIR::load(const std::string& path, double host_rate, const IRShape& shape,
                                         std::string* err) {
  WavData w;
  if (!read_wav(path, &w, err)) return nullptr;
  return make(w.channels, w.rate, host_rate, shape, err);
}

// ------------------------------------------------------------------------------------------------ ReverbEngine
float ReverbEngine::Biquad::run(float x) {                  // transposed direct form II
  const double y = b0 * x + z1;
  z1 = b1 * x - a1 * y + z2;
  z2 = b2 * x - a2 * y;
  return (float)y;
}

ReverbEngine::ReverbEngine(double host_rate) : rate_(host_rate) {
  const size_t pd = (size_t)(kMaxPredelay * host_rate / 1000.0) + 256;
  for (auto& v : pd_) v.assign(pd, 0.0f);
  for (int c = 0; c < 2; c++) { fin_[c].assign(128, 0.0f); fout_[c].assign(128, 0.0f); }
  update_filters();
}

ReverbEngine::~ReverbEngine() {
  delete pending_.exchange(nullptr);
  delete cur_;
  delete old_;
  delete hold_;
  collect();
}

void ReverbEngine::set_ir(std::unique_ptr<ReverbIR> ir) {
  if (!ir) clear_ = true;                                   // no IR: the audio thread fades the current one out
  else clear_ = false;                                      // (a newer IR replaces a pending clear)
  delete pending_.exchange(ir.release());                   // one the audio thread never took: safe to free here
}

void ReverbEngine::retire(ReverbIR* p) {                    // audio thread: hand over for freeing, never free here
  if (!p) return;
  for (auto& slot : retired_) {
    ReverbIR* expect = nullptr;
    if (slot.compare_exchange_strong(expect, p)) return;
  }
  if (!hold_) hold_ = p;                                    // ring full: keep it (freed later)
}

void ReverbEngine::collect() {
  for (auto& slot : retired_) delete slot.exchange(nullptr);
}

void ReverbEngine::update_filters() {
  const float lc = params.lowcut_hz, hc = params.highcut_hz;
  if (lc == lc_cache_ && hc == hc_cache_) return;
  lc_cache_ = lc; hc_cache_ = hc;
  auto design = [this](Biquad& f, double fc, bool high) {   // RBJ, Q = 1/sqrt(2) (Butterworth)
    fc = std::min(std::max(fc, 10.0), 0.45 * rate_);
    const double w = 2 * M_PI * fc / rate_, cs = std::cos(w), al = std::sin(w) / (2 * M_SQRT1_2), a0 = 1 + al;
    const double k = high ? (1 + cs) / 2 : (1 - cs) / 2;
    f.b0 = k / a0; f.b1 = (high ? -(1 + cs) : (1 - cs)) / a0; f.b2 = k / a0;
    f.a1 = -2 * cs / a0; f.a2 = (1 - al) / a0;
  };
  for (int c = 0; c < 2; c++) { design(hp_[c], lc, true); design(lp_[c], hc, false); }
}

void ReverbEngine::block(const float* in_l, const float* in_r, float* out_l, float* out_r) {
  const int B = 128;
  if (clear_.exchange(false) && cur_) {                     // no IR: crossfade the current one out to nothing
    retire(old_);
    old_ = cur_;
    cur_ = nullptr;
    fade_ = 0;
  }
  if (ReverbIR* p = pending_.exchange(nullptr)) {           // a new IR: crossfade from the current one
    retire(old_);
    old_ = cur_;
    cur_ = p;
    fade_ = 0;
  }
  if (hold_) { ReverbIR* h = hold_; hold_ = nullptr; retire(h); }
  update_filters();
  const float mix = std::min(1.0f, std::max(0.0f, (float)params.mix));
  const float width = std::min(2.0f, std::max(0.0f, (float)params.width));
  const float gain = (float)std::pow(10.0, params.output_db / 20.0);
  const size_t pdn = pd_[0].size();
  const size_t delay = std::min(pdn - B - 1, (size_t)std::lround(std::max(0.0f, (float)params.predelay_ms) * rate_ / 1000.0));
  const bool stereo_ir = cur_ && cur_->stereo;
  for (int i = 0; i < B; i++) {                             // pre-delay (the mono sum for a stereo IR)
    const size_t w = (pd_pos_ + i) % pdn, rd = (pd_pos_ + i + pdn - delay) % pdn;
    if (stereo_ir) { pd_[0][w] = 0.5f * (in_l[i] + in_r[i]); xl_[i] = pd_[0][rd]; xr_[i] = xl_[i]; }
    else { pd_[0][w] = in_l[i]; pd_[1][w] = in_r[i]; xl_[i] = pd_[0][rd]; xr_[i] = pd_[1][rd]; }
  }
  pd_pos_ = (pd_pos_ + B) % pdn;
  const float fb = std::min(0.9f, std::max(0.0f, (float)params.feedback));
  if (fb > 0.0f)                                            // feedback: last block's wet output, soft-limited (tanh)
    for (int i = 0; i < B; i++) { xl_[i] += fb * fbl_[i]; xr_[i] += fb * (stereo_ir ? fbl_[i] : fbr_[i]); }
  if (cur_) {
    cur_->l->process(xl_, wl_, B);
    cur_->r->process(cur_->stereo ? xl_ : xr_, wr_, B);
  } else {
    memset(wl_, 0, sizeof wl_); memset(wr_, 0, sizeof wr_);
  }
  if (old_) {                                               // the outgoing IR, fading out
    old_->l->process(xl_, ol_, B);
    old_->r->process(old_->stereo ? xl_ : xr_, or_, B);
    for (int i = 0; i < B; i++) {
      const float g = std::min(1.0f, (float)(fade_ + i) / kFade);
      wl_[i] = wl_[i] * g + ol_[i] * (1 - g);
      wr_[i] = wr_[i] * g + or_[i] * (1 - g);
    }
    fade_ += B;
    if (fade_ >= kFade) { retire(old_); old_ = nullptr; }
  }
  for (int i = 0; i < B; i++) {                             // for the next block's feedback (bounded by tanh)
    fbl_[i] = std::tanh(stereo_ir ? 0.5f * (wl_[i] + wr_[i]) : wl_[i]);
    fbr_[i] = std::tanh(wr_[i]);
  }
  if (cur_) {                                               // late tails, per IR
    const long m = cur_->l->misses() + cur_->r->misses();
    if (m > cur_->reported) { misses_ += m - cur_->reported; cur_->reported = m; }
  }
  {                                                         // the playhead: a note restarts it; it crosses the IR
    float pk = 0;
    for (int i = 0; i < B; i++) pk = std::max(pk, std::max(std::fabs(in_l[i]), std::fabs(in_r[i])));
    if (pk > 0.01f && pk > 2.0f * env_) play_t_ = 0;          // a note: louder than -40 dBFS and 6 dB over the recent level
    else play_t_ += B;
    env_ = std::max(pk, env_ * 0.98f);
    int col = 0;
    if (cur_) {
      const long total = (long)delay + (long)cur_->frames;
      if (play_t_ < total) col = 1 + (int)((long long)kPlay * play_t_ / std::max(1L, total));
    }
    playhead_.store(std::min(col, kPlay), std::memory_order_relaxed);
  }
  const float target = cur_ ? 1.0f : 0.0f;                  // the wet share eases in and out with the IR
  for (int i = 0; i < B; i++) {
    // a linear ramp (~46 ms), which ends exactly on target. (An exponential ease in float stalls just short of
    // it: near 1.0 its steps round away, which left the dry signal at about -78 dB at mix 100%.)
    const float step = 1.0f / 2048.0f;
    presence_ = presence_ < target ? std::min(target, presence_ + step) : std::max(target, presence_ - step);
    float l = lp_[0].run(hp_[0].run(wl_[i])), r = lp_[1].run(hp_[1].run(wr_[i]));
    const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;   // width: mid/side
    l = m + s; r = m - s;
    const float dry = 1.0f - mix * presence_;
    out_l[i] = gain * (dry * in_l[i] + mix * l);
    out_r[i] = gain * (dry * in_r[i] + mix * r);
  }
}

void ReverbEngine::process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n) {
  FlushDenormals fd;
  if (!in_r) in_r = in_l;
  if (fifo_fill_ == 0 && n % 128 == 0) {                    // MPC: whole 128-frame blocks, no added latency
    for (int off = 0; off < n; off += 128) block(in_l + off, in_r + off, out_l + off, out_r + off);
    return;
  }
  for (int i = 0; i < n; i++) {                             // any other block size: one block of latency
    fin_[0][fifo_fill_] = in_l[i]; fin_[1][fifo_fill_] = in_r[i];
    out_l[i] = fout_[0][fifo_fill_]; out_r[i] = fout_[1][fifo_fill_];
    if (++fifo_fill_ == 128) {
      block(fin_[0].data(), fin_[1].data(), fout_[0].data(), fout_[1].data());
      fifo_fill_ = 0;
    }
  }
}

}  // namespace irrev
