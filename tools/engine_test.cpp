// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// engine_test.cpp [dir with make_test_wavs.py output] -- what ReverbEngine adds around the convolution (which
// bench_reverb --check verifies against direct convolution): pass-through, mix, pre-delay, mono/stereo IRs, width,
// level normalisation, IR crossfade, odd host block sizes, loading WAVs. IRs built with LongConvolver::Inline (the
// same arithmetic as Threaded, deterministic without real-time pacing).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "reverb_engine.h"
using namespace irrev;
static int fails = 0;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static const double RATE = 44100;

static std::unique_ptr<ReverbIR> ir_of(std::vector<std::vector<float>> ch, double length = 1.0) {
  std::string err;
  IRShape s; s.length = length;
  return ReverbIR::make(ch, RATE, RATE, s, &err, LongConvolver::Inline);
}
static std::unique_ptr<ReverbIR> ir_shaped(std::vector<std::vector<float>> ch, const IRShape& s) {
  std::string err;
  return ReverbIR::make(ch, RATE, RATE, s, &err, LongConvolver::Inline);
}
// the engine's response to a single impulse (mix 1, filters open): where its two largest peaks are
static std::vector<float> impulse_response(std::unique_ptr<ReverbIR> ir, size_t n) {
  ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000;
  e.set_ir(std::move(ir));
  std::vector<float> x(n, 0.0f), ol, orr; x[128 * 20] = 1.0f;   // after the wet fade-in (2048 samples)
  ol.assign(n, 0); orr.assign(n, 0);
  for (size_t p = 0; p < n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
  return std::vector<float>(ol.begin() + 128 * 20, ol.end());
}
static size_t peak_at(const std::vector<float>& y, size_t lo, size_t hi) {
  size_t k = lo; for (size_t i = lo; i < hi && i < y.size(); i++) if (std::fabs(y[i]) > std::fabs(y[k])) k = i; return k;
}
static std::vector<float> decay(size_t n, unsigned seed, float level) {
  std::mt19937 r(seed); std::normal_distribution<float> g(0, 1);
  std::vector<float> v(n);
  for (size_t i = 0; i < n; i++) v[i] = level * g(r) * std::exp(-6.9f * i / n);
  return v;
}
static double rms(const std::vector<float>& v, size_t from = 0) {
  double a = 0; for (size_t i = from; i < v.size(); i++) a += (double)v[i] * v[i];
  return std::sqrt(a / std::max<size_t>(1, v.size() - from));
}
// run n frames (in blocks of `blk`) of stereo input through e
static void run(ReverbEngine& e, const std::vector<float>& l, const std::vector<float>& r, std::vector<float>& ol,
                std::vector<float>& orr, int blk = 128) {
  ol.assign(l.size(), 0); orr.assign(l.size(), 0);
  for (size_t p = 0; p < l.size(); p += blk) {
    int n = (int)std::min<size_t>(blk, l.size() - p);
    e.process(&l[p], &r[p], &ol[p], &orr[p], n);
  }
}

int main(int argc, char** argv) {
  const size_t N = 128 * 400;
  std::mt19937 rg(1); std::normal_distribution<float> g(0, 0.1f);
  std::vector<float> nl(N), nr(N), ol, orr;
  for (size_t i = 0; i < N; i++) { nl[i] = g(rg); nr[i] = g(rg); }

  { ReverbEngine e(RATE); run(e, nl, nr, ol, orr);
    CHECK(ol == nl && orr == nr, "no IR: the dry signal passes untouched"); }

  { ReverbEngine e(RATE); e.params.mix = 0; e.set_ir(ir_of({decay(44100, 2, 0.2f)}));
    run(e, nl, nr, ol, orr);
    double d = 0; for (size_t i = 0; i < N; i++) d = std::max(d, (double)std::fabs(ol[i] - nl[i]));
    CHECK(d < 1e-6, "mix 0 with an IR: the dry signal (max diff %.1e)", d); }

  { // pre-delay: an impulse IR, mix 1, filters open: an input impulse comes out pre-delay later
    ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000; e.params.predelay_ms = 50;
    std::vector<float> h(256, 0.0f); h[0] = 1.0f;
    e.set_ir(ir_of({h}));
    std::vector<float> x(N, 0.0f); x[128 * 20] = 1.0f;
    run(e, x, x, ol, orr);
    size_t pk = std::max_element(ol.begin(), ol.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); }) - ol.begin();
    const long want = 128 * 20 + std::lround(0.05 * RATE);
    CHECK(std::labs((long)pk - want) <= 2, "pre-delay 50 ms: the impulse comes out %ld samples later (want %ld)", (long)pk - 128 * 20, want - 128 * 20); }

  { // a mono IR keeps L and R apart; a stereo IR spreads; width 0 = mono
    ReverbEngine e(RATE); e.params.mix = 1;
    e.set_ir(ir_of({decay(22050, 3, 0.2f)}));
    std::vector<float> z(N, 0.0f);
    run(e, nl, z, ol, orr);
    CHECK(rms(ol, N / 2) > 0.01 && rms(orr, N / 2) < 1e-6, "mono IR: L in -> L out only (R rms %.1e)", rms(orr, N / 2));
    ReverbEngine s(RATE); s.params.mix = 1;
    s.set_ir(ir_of({decay(22050, 4, 0.2f), decay(22050, 5, 0.2f)}));
    run(s, nl, z, ol, orr);
    double corr = 0, a = 0, b = 0; for (size_t i = N / 2; i < N; i++) { corr += ol[i] * orr[i]; a += ol[i] * ol[i]; b += orr[i] * orr[i]; }
    CHECK(rms(orr, N / 2) > 0.01 && std::fabs(corr / std::sqrt(a * b)) < 0.5, "stereo IR: L in -> both sides, decorrelated (corr %.2f)", corr / std::sqrt(a * b));
    ReverbEngine w(RATE); w.params.mix = 1; w.params.width = 0;
    w.set_ir(ir_of({decay(22050, 4, 0.2f), decay(22050, 5, 0.2f)}));
    run(w, nl, nr, ol, orr);
    double d = 0; for (size_t i = N / 2; i < N; i++) d = std::max(d, (double)std::fabs(ol[i] - orr[i]));   // after the wet fade-in
    CHECK(d < 1e-6, "width 0 (mix 1, settled): L == R (max diff %.1e)", d); }

  { // two IRs 40 dB apart come out at about the same level (unit-energy normalisation)
    double lv[2];
    for (int k = 0; k < 2; k++) {
      ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000;
      e.set_ir(ir_of({decay(44100, 6, k ? 0.002f : 0.2f)}));
      run(e, nl, nr, ol, orr);
      lv[k] = 20 * std::log10(rms(ol, N / 2) / rms(nl, N / 2));
    }
    CHECK(std::fabs(lv[0] - lv[1]) < 0.1 && std::fabs(lv[0]) < 3, "IRs 40 dB apart: wet %.2f dB and %.2f dB of the input", lv[0], lv[1]); }

  { // switching IRs mid-stream: crossfaded, no jump
    ReverbEngine e(RATE); e.params.mix = 1;
    e.set_ir(ir_of({decay(22050, 7, 0.2f)}));
    std::vector<float> l(N), r(N);
    for (size_t i = 0; i < N; i++) l[i] = r[i] = 0.3f * (float)std::sin(2 * M_PI * 220 * i / RATE);
    ol.assign(N, 0); orr.assign(N, 0);
    for (size_t p = 0; p < N; p += 128) {
      if (p == 128 * 200) e.set_ir(ir_of({decay(22050, 8, 0.2f)}));
      e.process(&l[p], &r[p], &ol[p], &orr[p], 128);
    }
    double jump = 0, typical = 0;
    for (size_t i = 128 * 150 + 1; i < 128 * 260; i++) {
      double d = std::fabs(ol[i] - ol[i - 1]);
      if (i < 128 * 199) typical = std::max(typical, d); else jump = std::max(jump, d);
    }
    CHECK(jump < 2.0 * typical, "an IR switch crossfades: largest step %.3f (before: %.3f)", jump, typical);
    e.collect(); }

  { // odd host blocks: the same output, one block (128) later
    ReverbEngine a(RATE), b(RATE);
    a.params.mix = 0.5f; b.params.mix = 0.5f;
    a.set_ir(ir_of({decay(22050, 9, 0.2f)})); b.set_ir(ir_of({decay(22050, 9, 0.2f)}));
    std::vector<float> o1, o1r, o2, o2r;
    run(a, nl, nr, o1, o1r, 128);
    run(b, nl, nr, o2, o2r, 100);
    double d = 0; for (size_t i = 128; i < N; i++) d = std::max(d, (double)std::fabs(o2[i] - o1[i - 128]));
    CHECK(d < 1e-5, "host blocks of 100: the same output 128 samples later (max diff %.1e)", d); }

  { // length: a share of the IR; WAVs (stereo, EXTENSIBLE) load
    auto full = ir_of({decay(44100, 10, 0.2f)}, 1.0), half = ir_of({decay(44100, 10, 0.2f)}, 0.5);
    CHECK(full && half && half->frames == full->frames / 2, "Length 50%%: %zu of %zu frames", half ? half->frames : 0, full ? full->frames : 0);
    auto big = ir_of({decay(44100 * 8, 11, 0.2f)});
    CHECK(big && big->frames == (size_t)(ReverbIR::kMaxSeconds * RATE), "an 8 s IR is cut to %.0f s (%zu frames)", ReverbIR::kMaxSeconds, big ? big->frames : 0);
    if (argc > 1) {
      std::string dir = argv[1], err;
      IRShape full;
    auto st = ReverbIR::load(dir + "/stereo_ext_float.wav", RATE, full, &err);
      CHECK(st && st->stereo, "a stereo EXTENSIBLE float WAV loads as a stereo IR %s", err.c_str());
      auto mo = ReverbIR::load(dir + "/ext_pcm24.wav", RATE, full, &err);
      CHECK(mo && !mo->stereo, "a mono EXTENSIBLE WAV without fact loads %s", err.c_str());
    } }
  { // IR shaping (Kilohearts-style): a 2000-sample IR with a loud spike at 100 and a softer one at 1500
    std::vector<float> h(2000, 0.0f); h[100] = 1.0f; h[1500] = 0.5f;
    IRShape plain; plain.fade_out = 0;
    auto y = impulse_response(ir_shaped({h}, plain), 128 * 60);
    CHECK(peak_at(y, 0, 1000) == 100 && peak_at(y, 1000, 2000) == 1500, "as loaded: spikes at %zu and %zu", peak_at(y, 0, 1000), peak_at(y, 1000, 2000));
    IRShape rev = plain; rev.reverse = true;
    y = impulse_response(ir_shaped({h}, rev), 128 * 60);
    CHECK(peak_at(y, 0, 1000) == 499 && peak_at(y, 1000, 2000) == 1899 && std::fabs(y[1899]) > std::fabs(y[499]),
          "Reverse: the loud spike comes last (%zu), the soft one first (%zu)", peak_at(y, 1000, 2000), peak_at(y, 0, 1000));
    IRShape st = plain; st.start = 0.5;
    y = impulse_response(ir_shaped({h}, st), 128 * 60);
    double early = 0; for (size_t i = 0; i < 450; i++) early = std::max(early, (double)std::fabs(y[i]));
    CHECK(peak_at(y, 0, 1000) == 500 && early < 1e-6, "Start 50%%: only the later spike, now at %zu", peak_at(y, 0, 1000));
    IRShape sx = plain; sx.stretch = 2.0;
    auto ir2 = ir_shaped({h}, sx);
    const size_t frames2 = ir2 ? ir2->frames : 0;
    y = impulse_response(std::move(ir2), 128 * 80);
    const size_t p1 = peak_at(y, 0, 1000), p2 = peak_at(y, 1000, 4000);
    CHECK(frames2 >= 3990 && frames2 <= 4010 && p1 >= 198 && p1 <= 204 && p2 >= 2998 && p2 <= 3004,
          "Stretch 200%%: twice as long (%zu frames), spikes at %zu and %zu", frames2, p1, p2);
    IRShape fi = plain; fi.fade_in = 0.5;
    y = impulse_response(ir_shaped({h}, fi), 128 * 60);
    const double s_in = std::fabs(y[100]) / std::fabs(y[1500]);
    auto y0 = impulse_response(ir_shaped({h}, plain), 128 * 60);
    const double s_plain = std::fabs(y0[100]) / std::fabs(y0[1500]);
    CHECK(s_in < 0.2 * s_plain, "Fade in 50%%: the early spike much quieter (ratio %.3f vs %.3f)", s_in, s_plain);
  }
  { // set_ir(nullptr): the current IR fades out and the dry signal comes back untouched (Init, a failed load)
    ReverbEngine e(RATE); e.params.mix = 0.5f;
    e.set_ir(ir_of({decay(22050, 13, 0.2f)}));
    std::vector<float> o1, o1r;
    run(e, nl, nr, o1, o1r);
    e.set_ir(nullptr);
    std::vector<float> o2, o2r;
    run(e, nl, nr, o2, o2r);
    double d = 0; for (size_t i = N / 2; i < N; i++) d = std::max(d, (double)std::fabs(o2[i] - nl[i]));
    CHECK(d < 1e-6, "an IR cleared (set_ir(nullptr)) fades out to the dry signal (max diff %.1e)", d);
    e.collect();
  }
  { // feedback: a longer, bounded tail
    double tail[2]; bool finite = true; double peak = 0;
    for (int k = 0; k < 2; k++) {
      ReverbEngine e(RATE); e.params.mix = 1; e.params.feedback = k ? 0.9f : 0.0f;
      e.set_ir(ir_of({decay(22050, 12, 0.2f)}));
      const size_t n = 128 * 1200; std::vector<float> x(n, 0.0f), ol(n), orr(n);
      for (size_t i = 0; i < 128 * 100; i++) x[i] = g(rg);
      for (size_t p = 0; p < n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      double a = 0; for (size_t i = 128 * 600; i < n; i++) { a += ol[i] * ol[i]; if (!std::isfinite(ol[i])) finite = false; peak = std::max(peak, (double)std::fabs(ol[i])); }
      tail[k] = std::sqrt(a / (n - 128 * 600));
    }
    CHECK(finite && tail[1] > 10 * tail[0] && peak < 10, "Feedback 90%%: a longer tail (rms %.2e vs %.2e), bounded (peak %.2f)", tail[1], tail[0], peak);
  }
  { // sonics: decay time, brightness, width, measured when an IR is prepared
    std::mt19937 rs(31); std::normal_distribution<float> gs(0, 1);
    const size_t n = (size_t)(3.5 * RATE);
    std::vector<float> L(n), R(n), D(n);
    for (size_t i = 0; i < n; i++) { const float env = std::exp(-6.91f * (float)i / (float)(2.0 * RATE)); L[i] = gs(rs) * env; R[i] = gs(rs) * env; }
    float lp = 0; for (size_t i = 0; i < n; i++) { lp = 0.05f * L[i] + 0.95f * lp; D[i] = lp * 8; }   // a dark copy (~350 Hz)
    IRShape keep; keep.fade_out = 0;
    auto st = ir_shaped({L, R}, keep), mono = ir_shaped({L}, keep), dark = ir_shaped({D, D}, keep);
    CHECK(st && std::fabs(st->t60 - 2.0f) < 0.25f, "decay of a 2.0 s room measured as %.2f s", st ? st->t60 : 0.f);
    CHECK(st && dark && dark->bright < 0.25f * st->bright, "brightness: dark %.3f, white noise %.3f", dark ? dark->bright : 0.f, st ? st->bright : 0.f);
    CHECK(st && mono && st->width > 0.9f && mono->width == 0.0f && dark->width < 0.05f,
          "width: independent L/R %.2f, mono %.2f, identical L/R %.2f", st ? st->width : 0.f, mono ? mono->width : 0.f, dark ? dark->width : 0.f);
    int bright_early = 0;
    for (int c = 0; c < 6; c++) bright_early += st->tint[c];
    CHECK(st && bright_early >= 6, "white-noise room: its strands are neutral or bright (%d)", bright_early);
    IRShape rv = keep; rv.reverse = true;
    auto sw = ir_shaped({L, R}, rv);
    CHECK(st && st->swell == 0.0f && sw && sw->swell > 2.5f, "a reversed room swells (to %.2f s); the room itself doesn't", sw ? sw->swell : 0.f);
  }
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
