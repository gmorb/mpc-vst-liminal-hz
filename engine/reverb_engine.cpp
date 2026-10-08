// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "reverb_engine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <cstring>
#include "Resample.h"
#include "pffft.h"
#include "wav_reader.h"

namespace irrev {

float ReverbEngine::kDampHz = 4000.0f;   // feedback damping: repeats lose their top a little each pass (no 3 kHz whistle)
float ReverbEngine::kFbMax = 0.97f, ReverbEngine::kFbEase = 0.93f, ReverbEngine::kFbCeil = 1.4f, ReverbEngine::kFbRelease = 4.0f;

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

// ---- Decay: the IR's decay time scaled band by band (third octaves), off the audio thread ----------------------------
// Each band's decay time T is measured (Schroeder, -5..-35 dB); the band is then re-enveloped by exp(6.91 t (1 - 1/m) / T)
// so it decays in m x T. Lengthening (m > 1): where the band's real tail reaches its noise floor (or the file ends) it
// is continued with noise of the same spectrum, decaying at the new rate (random phase: a tail, not a loop). Done on
// an STFT (2048 / hop 512, sqrt-Hann). Returns the new length; m == 1 is never called.
static size_t decay_reshape(std::vector<std::vector<float>>& h, double rate, double m, size_t max_len, std::mt19937& rng) {
  const int N = 2048, H = 512, K = N / 2 + 1;
  if (h[0].size() > max_len) for (auto& v : h) v.resize(max_len);   // (never longer than the result may be)
  const size_t len = h[0].size();
  const size_t out_len = m > 1 ? std::min(max_len, std::max(len, (size_t)std::llround((double)len * m))) : len;
  const int F0 = (int)((len + N) / H) + 1, F = (int)((out_len + N) / H) + 1;   // frames over the original / the result
  const size_t nch = h.size();
  std::vector<float> win(N);
  for (int i = 0; i < N; i++) win[i] = (float)std::sqrt(0.5 * (1 - std::cos(2 * M_PI * i / N)));
  PFFFT_Setup* st = pffft_new_setup(N, PFFFT_REAL);
  float* buf = (float*)pffft_aligned_malloc(N * sizeof(float));
  float* wk = (float*)pffft_aligned_malloc(N * sizeof(float));
  // spectra: X[c][f][k] (re, im); frame f covers samples [f*H - N + H, f*H + H)
  std::vector<std::vector<std::vector<std::complex<float>>>> X(nch, std::vector<std::vector<std::complex<float>>>(F, std::vector<std::complex<float>>(K)));
  auto at = [&](size_t c, long i) { return i >= 0 && (size_t)i < len ? h[c][i] : 0.0f; };
  for (size_t c = 0; c < nch; c++)
    for (int f = 0; f < F0 && f < F; f++) {
      const long s0 = (long)f * H - N + H;
      for (int i = 0; i < N; i++) buf[i] = at(c, s0 + i) * win[i];
      pffft_transform_ordered(st, buf, buf, wk, PFFFT_FORWARD);
      X[c][f][0] = {buf[0], 0}; X[c][f][N / 2] = {buf[1], 0};
      for (int k = 1; k < N / 2; k++) X[c][f][k] = {buf[2 * k], buf[2 * k + 1]};
    }
  // third-octave bands from 40 Hz (everything below in the first)
  std::vector<int> kb;                                      // band b is bins [kb[b], kb[b + 1])
  { double lo = 40; kb.push_back(0);
    while (lo < rate / 2) { const int k = std::min(K, (int)std::ceil(lo * N / rate)); if (k > kb.back()) kb.push_back(k); lo *= std::pow(2.0, 1.0 / 3); }
    if (kb.back() < K) kb.push_back(K); }
  const int nb = (int)kb.size() - 1;
  std::uniform_real_distribution<float> ph(0.0f, (float)(2 * M_PI));
  for (int b = 0; b < nb; b++) {
    std::vector<double> E(F0, 0.0);
    const int k0 = kb[b], k1 = kb[b + 1];
    if (k1 <= k0) continue;
    for (size_t c = 0; c < nch; c++) for (int f = 0; f < F0; f++) for (int k = k0; k < k1; k++) E[f] += std::norm(X[c][f][k]);
    std::vector<double> edc(F0 + 1, 0.0);
    for (int f = F0 - 1; f >= 0; f--) edc[f] = edc[f + 1] + E[f];
    if (!(edc[0] > 1e-24)) continue;
    int f5 = -1, f35 = -1, f25 = -1;
    for (int f = 0; f < F0; f++) {
      const double d = 10 * std::log10(edc[f] / edc[0] + 1e-30);
      if (f5 < 0 && d <= -5) f5 = f;
      if (f25 < 0 && d <= -25) f25 = f;
      if (f35 < 0 && d <= -35) { f35 = f; break; }
    }
    double T;                                                // the band's decay time, s
    if (f5 >= 0 && f35 > f5) T = 2.0 * (f35 - f5) * H / rate;          // 30 dB x 2
    else if (f5 >= 0 && f25 > f5) T = 3.0 * (f25 - f5) * H / rate;     // 20 dB x 3
    else continue;                                           // too little decay to measure: left as it is
    T = std::max(0.05, T);
    // where the band's own tail ends: its level (smoothed) last 30 dB under its peak, or the file's end
    double pk = 0; for (int f = 0; f < F0; f++) pk = std::max(pk, E[f]);
    int fend = 0;
    for (int f = 0; f < F0; f++) { double sm = 0; int c2 = 0; for (int j = std::max(0, f - 2); j <= std::min(F0 - 1, f + 2); j++) { sm += E[j]; c2++; } if (sm / c2 > pk * 1e-3) fend = f; }
    const double kdec = 6.91 * (1 - 1 / m) / T;              // amplitude: exp(kdec * t)
    std::vector<std::vector<float>> ref(nch, std::vector<float>(K, 0.0f));   // the tail's spectrum just before fend
    if (m > 1) for (size_t c = 0; c < nch; c++) for (int k = k0; k < k1; k++) {
      double a = 0; int c3 = 0;
      for (int f = std::max(0, fend - 4); f <= fend; f++) { a += std::sqrt(std::norm(X[c][f][k])); c3++; }   // (sqrt(norm), not abs: hypotf needs glibc 2.35)
      ref[c][k] = (float)(a / c3);
    }
    const double tend = (double)fend * H / rate;
    for (int f = 0; f < F; f++) {
      const double t = (double)f * H / rate;
      const float g = (float)std::exp(kdec * std::min(t, tend + (m > 1 ? 0.0 : 1e9)));
      // past the band's tail (lengthening): the continuation, faded in over 4 frames as the real one fades out
      const float syn = m > 1 ? (float)std::min(1.0, std::max(0.0, (f - fend) / 4.0)) : 0.0f;
      const float lvl = m > 1 ? (float)(std::exp(kdec * tend) * std::exp(-6.91 * std::max(0.0, t - tend) / (m * T))) : 0.0f;
      for (int k = k0; k < k1; k++)
        for (size_t c = 0; c < nch; c++) {
          std::complex<float> v = f < F0 ? X[c][f][k] * g : std::complex<float>(0, 0);
          // (frames of random phase add in power, not amplitude: at hop N/4 with sqrt-Hann both ways that is
          //  sqrt(1.5) / 2 of a coherent frame's level, so the continuation is lifted by 2 / sqrt(1.5))
          if (syn > 0) v = v * (1 - syn) + std::polar(ref[c][k] * lvl * syn * 1.633f, ph(rng));
          X[c][f][k] = v;
        }
    }
  }
  // back to samples (sqrt-Hann at hop N/4: the windows' product sums to 2)
  std::vector<std::vector<float>> out(nch, std::vector<float>(out_len + N, 0.0f));
  for (size_t c = 0; c < nch; c++)
    for (int f = 0; f < F; f++) {
      buf[0] = X[c][f][0].real(); buf[1] = X[c][f][N / 2].real();
      for (int k = 1; k < N / 2; k++) { buf[2 * k] = X[c][f][k].real(); buf[2 * k + 1] = X[c][f][k].imag(); }
      pffft_transform_ordered(st, buf, buf, wk, PFFFT_BACKWARD);
      const long s0 = (long)f * H - N + H;
      for (int i = 0; i < N; i++) { const long j = s0 + i; if (j >= 0 && (size_t)j < out_len) out[c][j] += buf[i] * win[i] / (float)N / 2.0f; }
    }
  for (size_t c = 0; c < nch; c++) { out[c].resize(out_len); h[c].swap(out[c]); }
  pffft_aligned_free(wk); pffft_aligned_free(buf); pffft_destroy_setup(st);
  return out_len;
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
  const size_t cap = (size_t)(kMaxSeconds * host_rate);
  double decay_level = 1.0;                                // Decay: the early part keeps its level, the tail adds (or takes)
  const double dm = std::min(3.0, std::max(0.5, shape.decay));
  if (dm != 1.0) {
    n = std::min(n, cap);                                  // (cap first: the reshaping assumes the IR fits in the cap;
    for (auto& v : h) v.resize(n, 0.0f);                   //  an IR over 5 s overran its frames before: a crash)
    auto energy = [&](size_t upto) { double e = 0; for (auto& v : h) for (size_t i = 0; i < std::min(upto, v.size()); i++) e += (double)v[i] * v[i]; return e; };
    const double e0 = energy(cap);
    std::mt19937 rng(12345);                               // (the same continuation every time the IR is prepared)
    n = decay_reshape(h, host_rate, dm, cap, rng);
    const double e1 = energy(cap);
    // half the tail's energy change, in dB: a longer decay is a little louder, as a bigger room is (+3 dB at 300%)
    if (e0 > 1e-20 && e1 > 1e-20) decay_level = std::min(1.5, std::max(0.5, std::pow(e1 / e0, 0.25)));
  }
  n = std::min(n, cap);                                    // the 5 s cap (after stretch: it's about CPU)
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
  const float g = (float)(decay_level / std::sqrt(e));
  for (auto& v : h) for (float& x : v) x *= g;
  std::unique_ptr<ReverbIR> ir(new ReverbIR());
  {                                                         // feedback: fb_gain = 1 / the loop's peak gain (2x zero-padded
    // spectrum), so Feedback's loop stays below 1 at every frequency. (Unit energy is about the average: a resonant or
    // bass-heavy IR peaks 8-30 dB over it, and the loop ran away and clipped from the knob's first quarter.)
    size_t N = 64;
    while (N < 2 * len) N <<= 1;
    PFFFT_Setup* st = pffft_new_setup((int)N, PFFFT_REAL);
    float* buf = (float*)pffft_aligned_malloc(N * sizeof(float));
    float* work = (float*)pffft_aligned_malloc(N * sizeof(float));
    if (st && buf && work) {
      memset(buf, 0, N * sizeof(float));                   // the loop's IR: the channels' mean (a stereo IR is fed the mono sum)
      for (size_t i = 0; i < len; i++) buf[i] = nch == 2 ? 0.5f * (h[0][i] + h[1][i]) : h[0][i];
      pffft_transform_ordered(st, buf, buf, work, PFFFT_FORWARD);
      double peak = 0;
      const double fcd = ReverbEngine::kDampHz, df = host_rate / (double)N;   // (with damping: the loop through its high cut)
      for (size_t k = 1; k < N / 2; k++) {
        const double lp = fcd > 0 ? 1.0 / (1.0 + std::pow(k * df / fcd, 4.0)) : 1.0;
        peak = std::max(peak, lp * ((double)buf[2 * k] * buf[2 * k] + (double)buf[2 * k + 1] * buf[2 * k + 1]));
      }
      peak = std::max(std::sqrt(peak), (double)std::fabs(buf[0]));
      ir->fb_gain = peak > 1e-9 ? (float)(1.0 / peak) : 0.0f;
    }
    if (work) pffft_aligned_free(work);
    if (buf) pffft_aligned_free(buf);
    if (st) pffft_destroy_setup(st);
  }
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
  if (kDampHz > 0) {                                        // feedback damping: a 2-pole high cut in the loop (RBJ, Butterworth)
    const double w = 2 * M_PI * std::min((double)kDampHz, 0.45 * host_rate) / host_rate, cs = std::cos(w), al = std::sin(w) / (2 * M_SQRT1_2), a0 = 1 + al;
    for (Biquad& f : damp_) { f.b0 = (1 - cs) / 2 / a0; f.b1 = (1 - cs) / a0; f.b2 = (1 - cs) / 2 / a0; f.a1 = -2 * cs / a0; f.a2 = (1 - al) / a0; }
  }
  for (int c = 0; c < 2; c++) { fin_[c].assign(128, 0.0f); fout_[c].assign(128, 0.0f); }
  onset_.reset(new OnsetDetector(host_rate));
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
  // a changed pre-delay crossfades from the old tap to the new one over the block (a jump in the tap clicked)
  const size_t dprev = delay_prev_ == (size_t)-1 ? delay : delay_prev_;
  for (int i = 0; i < B; i++) {                             // pre-delay (the mono sum for a stereo IR)
    const size_t w = (pd_pos_ + i) % pdn, rd = (pd_pos_ + i + pdn - delay) % pdn;
    if (stereo_ir) pd_[0][w] = 0.5f * (in_l[i] + in_r[i]);
    else { pd_[0][w] = in_l[i]; pd_[1][w] = in_r[i]; }
    float a = pd_[0][rd], b = pd_[stereo_ir ? 0 : 1][rd];
    if (dprev != delay) {
      const size_t ro = (pd_pos_ + i + pdn - dprev) % pdn;
      const float g = (float)(i + 1) / B;
      a = a * g + pd_[0][ro] * (1 - g);
      b = b * g + pd_[stereo_ir ? 0 : 1][ro] * (1 - g);
    }
    xl_[i] = a; xr_[i] = b;
  }
  delay_prev_ = delay;
  pd_pos_ = (pd_pos_ + B) % pdn;
  // feedback. The knob (0..1) sets the loop's strength on an eased curve, kFbMax * (1 - (1 - k)^2) (it builds quickly in
  // the first half). The loop is scaled by the IR's peak gain eased by kFbEase: 1 would keep the loop under 1 at every
  // frequency (safe, but weak on rooms: their loudest frequency sets the limit); below 1 the rest of the spectrum gets
  // more, and near the top of the knob the loudest frequencies may go over 1. Those are held by the guard below: the fed-back
  // signal is turned down whenever the wet signal goes over kFbCeil x the input's recent peak (released over kFbRelease s),
  // so a ringing frequency stays at about the input's level and dies away once the input stops (never an endless drone).
  {
    float pk = 0;
    for (int i = 0; i < B; i++) pk = std::max(pk, std::max(std::fabs(in_l[i]), std::fabs(in_r[i])));
    fb_env_ = std::max(pk, fb_env_ * (float)std::exp(-B / (kFbRelease * rate_)));
  }
  const float fk = std::min(1.0f, std::max(0.0f, (float)params.feedback));
  const float fb = kFbMax * (1.0f - (1.0f - fk) * (1.0f - fk)) * (cur_ ? std::pow(cur_->fb_gain, kFbEase) : 0.0f);
  if (fb > 0.0f) {                                          // last block's wet output (tanh: a last resort), under the guard
    const float r0 = fb_lim_prev_, r1 = fb_lim_;
    for (int i = 0; i < B; i++) {
      const float r = fb * (r0 + (r1 - r0) * (float)(i + 1) / B);
      float sl = fbl_[i], sr = stereo_ir ? fbl_[i] : fbr_[i];
      if (kDampHz > 0) { sl = damp_[0].run(sl); sr = stereo_ir ? sl : damp_[1].run(sr); }
      xl_[i] += r * sl; xr_[i] += r * sr;
    }
  }
  fb_lim_prev_ = fb_lim_;
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
  {                                                         // for the next block's feedback, and the guard's gain for it
    float wp = 0;
    for (int i = 0; i < B; i++) {
      fbl_[i] = std::tanh(stereo_ir ? 0.5f * (wl_[i] + wr_[i]) : wl_[i]);
      fbr_[i] = std::tanh(wr_[i]);
      wp = std::max(wp, std::max(std::fabs(wl_[i]), std::fabs(wr_[i])));
    }
    const float ceil = kFbCeil * fb_env_ + 1e-6f;
    const float want = wp > ceil ? ceil / wp : 1.0f;         // down at once, back up gently (about 0.1 s)
    fb_lim_ = want < fb_lim_ ? want : std::min(want, fb_lim_ + (float)B / (0.1f * (float)rate_));
  }
  if (cur_) {                                               // late tails, per IR
    const long m = cur_->l->misses() + cur_->r->misses();
    if (m > cur_->reported) { misses_ += m - cur_->reported; cur_->reported = m; }
  }
  {                                                         // the playhead: a note restarts it; it crosses the IR
    // a note: a MIDI note-on, or one heard in the input (onset.h: hits, soft attacks, pads over a release)
    const bool midi = note_.exchange(false, std::memory_order_relaxed);
    const bool heard = onset_->process(in_l, in_r, B);
    if (midi || heard) { play_t_ = 0; notes_.fetch_add(1, std::memory_order_relaxed); }
    else play_t_ += B;
    int col = 0;
    if (cur_) {
      const long total = (long)delay + (long)cur_->frames;
      if (play_t_ < total) col = 1 + (int)((long long)kPlay * play_t_ / std::max(1L, total));
    }
    playhead_.store(std::min(col, kPlay), std::memory_order_relaxed);
  }
  const float target = cur_ ? 1.0f : 0.0f;                  // the wet share eases in and out with the IR
  // mix, width and output glide to a new value over the block (a step from one block to the next zippered)
  const float mix0 = mix_prev_ < 0 ? mix : mix_prev_, width0 = width_prev_ < 0 ? width : width_prev_,
              gain0 = gain_prev_ < 0 ? gain : gain_prev_;
  mix_prev_ = mix; width_prev_ = width; gain_prev_ = gain;
  for (int i = 0; i < B; i++) {
    const float t = (float)(i + 1) / B;
    const float mix = mix0 + (mix_prev_ - mix0) * t, width = width0 + (width_prev_ - width0) * t,
                gain = gain0 + (gain_prev_ - gain0) * t;
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
