// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// engine_test.cpp [dir with make_test_wavs.py output] -- what ReverbEngine adds around the convolution (which
// bench_reverb --check verifies against direct convolution): pass-through, mix, pre-delay, mono/stereo IRs, width,
// level normalisation, IR crossfade, odd host block sizes, loading WAVs. IRs built with LongConvolver::Inline (the
// same arithmetic as Threaded, deterministic without real-time pacing).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "reverb_engine.h"
#include "wav_reader.h"
using namespace irrev;
static int fails = 0;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static const double RATE = 44100;

static std::unique_ptr<ReverbIR> ir_of(std::vector<std::vector<float>> ch, double length = 1.0) {
  std::string err;
  IRShape s; s.length = length;
  return ReverbIR::make(ch, RATE, RATE, s, &err, LongConvolver::Inline);
}
static std::unique_ptr<ReverbIR> ir_shaped(std::vector<std::vector<float>> ch, const IRShape& s,
                                           const Quality& q = platform::default_quality()) {
  std::string err;
  return ReverbIR::make(ch, RATE, RATE, s, &err, LongConvolver::Inline, q);
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
    const double over = ReverbIR::kMaxSeconds + 3.0;         // (8 s on Gen 1, 23 s on Gen 2)
    auto big = ir_of({decay((size_t)(44100 * over), 11, 0.2f)});
    CHECK(big && big->frames == (size_t)(ReverbIR::kMaxSeconds * RATE), "a %.0f s IR is cut to %.0f s (%zu frames)", over, ReverbIR::kMaxSeconds, big ? big->frames : 0);
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
      ReverbEngine e(RATE); e.params.mix = 1; e.params.feedback = k ? 1.0f : 0.0f;
      e.set_ir(ir_of({decay(22050, 12, 0.2f)}));
      const size_t n = 128 * 1200; std::vector<float> x(n, 0.0f), ol(n), orr(n);
      for (size_t i = 0; i < 128 * 100; i++) x[i] = g(rg);
      for (size_t p = 0; p < n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      double a = 0; for (size_t i = 128 * 600; i < n; i++) { a += ol[i] * ol[i]; if (!std::isfinite(ol[i])) finite = false; peak = std::max(peak, (double)std::fabs(ol[i])); }
      tail[k] = std::sqrt(a / (n - 128 * 600));
    }
    CHECK(finite && tail[1] > 10 * tail[0] && peak < 2, "Feedback 100%%: a longer tail (rms %.2e vs %.2e), bounded (peak %.2f)", tail[1], tail[0], peak);
  }
  { // feedback on a resonant IR (a ringing 110 Hz tone, its peak far above its average): the loop is scaled by the
    // IR's peak gain, so the level stays near the no-feedback level instead of running away into clipping
    std::vector<float> ring(44100);
    for (size_t i = 0; i < ring.size(); i++) ring[i] = std::sin(2 * M_PI * 110 * i / RATE) * std::exp(-3.0f * (float)i / 44100.0f);
    double rms[2], pk[2];
    for (int k = 0; k < 2; k++) {
      ReverbEngine e(RATE); e.params.mix = 1; e.params.feedback = k ? 1.0f : 0.0f;
      e.set_ir(ir_of({ring}));
      const size_t n = 128 * 2000; std::vector<float> x(n), ol(n), orr(n);
      std::mt19937 rr(5); std::normal_distribution<float> gg(0, 0.1f);
      for (size_t i = 0; i < n; i++) x[i] = gg(rr);
      for (size_t p = 0; p < n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      double a = 0; pk[k] = 0; for (size_t i = n / 2; i < n; i++) { a += ol[i] * ol[i]; pk[k] = std::max(pk[k], (double)std::fabs(ol[i])); }
      rms[k] = std::sqrt(a / (n / 2));
    }
    CHECK(rms[1] < 4 * rms[0] && pk[1] < 1.0, "Feedback 100%% on a resonant IR stays in bounds: rms %.3f vs %.3f without, peak %.2f", rms[1], rms[0], pk[1]);
    // and when the input stops, its ringing dies away (the guard follows the input down): no endless drone
    ReverbEngine e(RATE); e.params.mix = 1; e.params.feedback = 1.0f;
    e.set_ir(ir_of({ring}));
    const size_t n = (size_t)(RATE * 60); std::vector<float> x(n, 0.0f), ol(n), orr(n);
    std::mt19937 rr(9); std::normal_distribution<float> gg(0, 0.1f);
    for (size_t i = 0; i < (size_t)(RATE * 3); i++) x[i] = gg(rr);
    for (size_t p = 0; p + 128 <= n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
    auto rms_at = [&](double t0, double t1) { double a = 0; size_t c = 0; for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE); i++, c++) a += ol[i] * ol[i]; return std::sqrt(a / c); };
    const double playing = rms_at(1, 3), late = rms_at(57, 60);
    CHECK(late < playing * 1e-3, "Feedback 100%%: the ringing ends once the input stops (%.1f dB under the playing level after 54 s)", 20 * std::log10(playing / std::max(1e-12, late)));
  }
  { // feedback damping: each repeat is darker, so the fed-back part of the tail loses its top (a white room, Feedback 30%)
    auto tail_hf = [&](float damp) {                          // the tail's share of energy over 6 kHz, 0.6-1.4 s after the input
      const float keep = ReverbEngine::kDampHz; ReverbEngine::kDampHz = damp;
      std::mt19937 rr(6); std::normal_distribution<float> gg(0, 1);
      std::vector<float> h(22050);
      for (size_t i = 0; i < h.size(); i++) h[i] = gg(rr) * std::exp(-6.9f * (float)i / 22050.0f);
      ReverbEngine e(RATE); e.params.mix = 1; e.params.feedback = 0.3f; e.params.lowcut_hz = 20; e.params.highcut_hz = 20000;
      e.set_ir(ir_of({h}));
      const size_t n = (size_t)(RATE * 6); std::vector<float> x(n, 0.0f), ol(n), orr(n);
      std::mt19937 rx(2); std::normal_distribution<float> gx(0, 0.1f);
      for (size_t i = 0; i < (size_t)(RATE * 1); i++) x[i] = gx(rx);
      for (size_t p = 0; p + 128 <= n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      double all = 0, d = 0;                                  // first difference: a crude high-frequency measure
      for (size_t i = (size_t)(RATE * 1.6); i < (size_t)(RATE * 2.4); i++) { all += ol[i] * ol[i]; const double v = ol[i] - ol[i - 1]; d += v * v; }
      e.collect(); ReverbEngine::kDampHz = keep;
      return all > 0 ? d / all : 0.0;
    };
    const double off = tail_hf(0.0f), on = tail_hf(ReverbEngine::kDampHz);
    CHECK(on > 0 && on < 0.7 * off, "Feedback is damped: the fed-back tail is darker (brightness %.3f vs %.3f undamped)", on, off);
  }
  { // Decay (experimental): the decay time scaled band by band; 100% leaves the IR exactly as it was
    std::mt19937 rr(8); std::normal_distribution<float> gg(0, 1);
    const size_t n = (size_t)(1.6 * RATE);
    std::vector<float> L(n), Rr(n);
    for (size_t i = 0; i < n; i++) { const float env = std::exp(-6.91f * (float)i / (float)RATE); L[i] = gg(rr) * env; Rr[i] = gg(rr) * env; }   // T60 1.0 s
    auto t60 = [&](double d) { IRShape s; s.decay = d; s.fade_out = 0; auto ir = ir_shaped({L, Rr}, s); return ir ? ir->t60 : 0.0f; };
    auto frames = [&](double d) { IRShape s; s.decay = d; auto ir = ir_shaped({L, Rr}, s); return ir ? ir->frames : (size_t)0; };
    const float t1 = t60(1.0), th = t60(0.5), t2 = t60(2.0), t3 = t60(3.0);
    CHECK(std::fabs(t1 - 1.0f) < 0.15f && std::fabs(th - 0.5f) < 0.12f && t2 > 1.6f && t2 < 2.4f && t3 > 2.4f,
          "Decay 50/100/200/300%%: a 1.0 s room measures %.2f / %.2f / %.2f / %.2f s", th, t1, t2, t3);
    CHECK(frames(2.0) > frames(1.0) * 1.8 && frames(3.0) <= (size_t)(ReverbIR::kMaxSeconds * RATE),
          "Decay lengthens the IR (continued past its end), within the cap: %zu -> %zu, %zu frames", frames(1.0), frames(2.0), frames(3.0));
    {   // IRs of every length (past the 5 s cap too: one of 6 s crashed), mono and stereo, at every Decay
      bool all = true; size_t worst = 0;
      const double K = ReverbIR::kMaxSeconds;
      for (double secs : {0.01, 0.05, 0.3, K - 0.1, K, K + 1.0, K + 4.0})
        for (int ch = 1; ch <= 2; ch++)
          for (double d : {0.5, 0.75, 1.5, 2.0, 3.0}) {
            const size_t m2 = (size_t)(secs * RATE);
            std::vector<std::vector<float>> c(ch, std::vector<float>(m2));
            for (auto& v : c) for (size_t i = 0; i < m2; i++) v[i] = gg(rr) * std::exp(-3.0f * (float)i / (float)RATE);
            IRShape s; s.decay = d; auto ir = ir_shaped(c, s);
            if (!ir) all = false; else worst = std::max(worst, ir->frames);
          }
      CHECK(all && worst <= (size_t)(ReverbIR::kMaxSeconds * RATE), "Decay on IRs of 0.01-%.0f s, mono and stereo: all prepared, none over the cap (%zu frames)", K + 4.0, worst);
    }
    IRShape a1; auto i1 = ir_shaped({L, Rr}, a1), i2 = ir_shaped({L, Rr}, a1);
    CHECK(i1 && i2 && i1->sonics == i2->sonics && i1->t60 == i2->t60, "Decay 100%%: the IR as prepared before (the reshaping is skipped)");
    {   // band-limited IRs at Stretch x Decay: Grain Cloud and Tuned Chord (nothing much above ~5 kHz; Stretch 200% on
        // Gen 1 halves that again) were "Silent IR" at Stretch 200% with any Decay but 100%: an all but empty band's
        // "decay time" is noise, and lifting it overflowed a float (inf, then NaN). The factory files themselves.
      bool all = true; int tried = 0; std::string what;
      for (const char* f : {"packaging/factory/Textures/Grain_Cloud.wav", "packaging/factory/Textures/Tuned_Chord.wav",
                            "packaging/factory/Textures/Struck_Pipe.wav", "packaging/factory/Spaces/Backrooms.wav"}) {
        WavData w; std::string err;
        if (!read_wav(f, &w, &err)) continue;
        for (double st : {0.5, 1.0, 1.5, 2.0})
          for (double d : {0.5, 1.5, 2.5, 3.0})
            for (int hq = 0; hq < 2; hq++) {
              IRShape s; s.stretch = st; s.decay = d; tried++;
              auto ir = ReverbIR::make(w.channels, w.rate, RATE, s, &err, LongConvolver::Inline, hq ? Quality::high() : platform::default_quality());
              if (!ir || !std::isfinite(ir->t60)) { all = false; char b[160]; snprintf(b, sizeof b, " [%s stretch %.1f decay %.1f hq %d: %s]", f + 18, st, d, hq, err.c_str()); what += b; }
            }
      }
      CHECK(all && tried == 128, "the factory Textures prepare at every Stretch x Decay, standard and high quality (%d tried)%s", tried, what.c_str());
    }
  }
  { // high quality (LONG IR on Gen 1, always on Gen 2; platform.h): long IRs, Decay past 5 s, band-limited resampling
    const Quality HQ = Quality::high();
    auto impulse_response = [](ReverbIR& ir, size_t n) {      // the prepared IR, read back through its convolver
      n = (n + 127) / 128 * 128;
      std::vector<float> x(n, 0.0f), y(n, 0.0f); x[0] = 1.0f;
      ir.l->process(x.data(), y.data(), (int)n);
      return y;
    };
    auto amp = [](const std::vector<float>& y, double f, double rate, size_t a, size_t b) {   // the level of f in y[a, b)
      double re = 0, im = 0;
      for (size_t i = a; i < b && i < y.size(); i++) { const double w = 2 * M_PI * f * i / rate; re += y[i] * std::cos(w); im += y[i] * std::sin(w); }
      return 2 * std::sqrt(re * re + im * im) / (double)(b - a);
    };
    std::string err;
    {
      IRShape s15;
      auto ir = ir_shaped({decay((size_t)(44100 * 15), 21, 0.2f)}, s15, HQ);
      CHECK(ir && ir->frames == (size_t)(15 * RATE) && ir->l->stages() == 2,
            "a 15 s IR is used whole, with the far convolution stage (%zu frames, %zu stages)", ir ? ir->frames : 0, ir ? ir->l->stages() : 0);
    }
    {   // a 5 s room at Decay 300%: on Gen 1 it stopped at the 5 s cap; here it rings on
      std::mt19937 r5(9); std::normal_distribution<float> g5(0, 1);
      const size_t n5 = (size_t)(5 * RATE);
      std::vector<float> L5(n5);
      for (size_t i = 0; i < n5; i++) L5[i] = g5(r5) * std::exp(-3.45f * (float)i / (float)RATE);   // T60 2.0 s, recorded 5 s
      IRShape s; s.decay = 3.0; s.fade_out = 0;
      auto ir = ir_shaped({L5}, s, HQ);
      CHECK(ir && ir->frames > (size_t)(12 * RATE) && ir->t60 > 4.5f,
            "Decay 300%% on a 5 s IR goes past 5 s: %.1f s long, decay %.1f s", ir ? ir->frames / RATE : 0.0, ir ? ir->t60 : 0.0f);
    }
    {   // 48 kHz IR to 44.1 kHz: 1 kHz and 17 kHz at the same level stay at the same level (cubic dulls 17 kHz)
      const double r48 = 48000; const size_t n = (size_t)(0.5 * r48);
      std::vector<float> c(n);
      for (size_t i = 0; i < n; i++) c[i] = (float)((std::sin(2 * M_PI * 1000 * i / r48) + std::sin(2 * M_PI * 17000 * i / r48)) * std::exp(-2.0 * i / r48));
      IRShape s; s.fade_out = 0;
      auto ir = ReverbIR::make({c}, r48, RATE, s, &err, LongConvolver::Inline, HQ);
      double db = -99;
      if (ir) { auto y = impulse_response(*ir, ir->frames); db = 20 * std::log10(amp(y, 17000, RATE, 2000, 12000) / amp(y, 1000, RATE, 2000, 12000)); }
      CHECK(ir && std::fabs(db) < 0.3, "sinc resampling 48 -> 44.1 kHz keeps 17 kHz level with 1 kHz (%+.2f dB)", db);
    }
    {   // the resampling is kept between preparations (a trim or fade doesn't resample again): the same IR either way
      const double r48 = 48000; const size_t n = (size_t)(1.5 * r48);
      std::vector<float> c(n);
      for (size_t i = 0; i < n; i++) c[i] = (float)(std::sin(0.37 * i) * std::exp(-3.0 * i / r48));
      IRShape a1, a2; a2.length = 0.5;
      auto x1 = ReverbIR::make({c}, r48, RATE, a1, &err, LongConvolver::Inline, HQ);    // resampled
      auto x2 = ReverbIR::make({c}, r48, RATE, a2, &err, LongConvolver::Inline, HQ);    // from the kept resampling
      auto x3 = ReverbIR::make({c}, r48, RATE, a1, &err, LongConvolver::Inline, HQ);    // as the first
      bool same = x1 && x3 && x1->frames == x3->frames && x1->sonics == x3->sonics;
      if (same) { auto y1 = impulse_response(*x1, x1->frames), y3 = impulse_response(*x3, x3->frames); same = y1 == y3; }
      CHECK(same && x2 && x2->frames == x1->frames / 2, "a kept resampling gives the same IR (%zu, %zu frames; End 50%%: %zu)",
            x1 ? x1->frames : 0, x3 ? x3->frames : 0, x2 ? x2->frames : 0);
      // several are kept: back to the first IR after three others is still quick, and still the same IR
      using C = std::chrono::steady_clock;
      auto ms = [](C::time_point a) { return std::chrono::duration<double, std::milli>(C::now() - a).count(); };
      std::vector<std::vector<float>> others(3, c);
      for (int k = 0; k < 3; k++) for (float& v : others[k]) v *= 0.5f + 0.1f * k;   // three different IRs
      std::vector<float> c8((size_t)(8 * r48)); for (size_t i = 0; i < c8.size(); i++) c8[i] = c[i % n];
      auto t0 = C::now(); auto f1 = ReverbIR::make({c8}, r48, RATE, a1, &err, LongConvolver::Inline, HQ); const double cold = ms(t0);
      for (auto& o : others) ReverbIR::make({o}, r48, RATE, a1, &err, LongConvolver::Inline, HQ);
      t0 = C::now(); auto f2 = ReverbIR::make({c8}, r48, RATE, a1, &err, LongConvolver::Inline, HQ); const double warm = ms(t0);
      bool same2 = f1 && f2 && f1->frames == f2->frames;
      if (same2) { auto y1 = impulse_response(*f1, f1->frames), y2 = impulse_response(*f2, f2->frames); same2 = y1 == y2; }
      CHECK(same2 && warm < 0.7 * cold, "4 resamplings kept: an 8 s 48 kHz IR again after 3 others, %.0f ms (first time %.0f ms), the same IR",
            warm, cold);
    }
    {   // the standard quality (Gen 1 with LONG IR off) keeps 5 s, and a 5 s IR also uses the far stage (both builds)
      IRShape s0;
      auto lo = ir_shaped({decay((size_t)(44100 * 12), 22, 0.2f)}, s0, Quality::standard());
      auto hi = ir_shaped({decay((size_t)(44100 * 12), 22, 0.2f)}, s0, HQ);
      CHECK(lo && hi && lo->frames == (size_t)(5 * RATE) && hi->frames == (size_t)(12 * RATE) && lo->l->stages() == 2,
            "a 12 s IR: %zu frames standard (5 s), %zu at high quality (12 s); far stage at 5 s: %zu stages",
            lo ? lo->frames : 0, hi ? hi->frames : 0, lo ? lo->l->stages() : 0);
    }
    {   // Stretch 50% (resampled to half the rate, played at the full one): 15 kHz can't fit, so it must not fold back
      const size_t n = (size_t)(0.5 * RATE);
      std::vector<float> c(n);
      for (size_t i = 0; i < n; i++) c[i] = (float)((std::sin(2 * M_PI * 1000 * i / RATE) + std::sin(2 * M_PI * 15000 * i / RATE)) * std::exp(-2.0 * i / RATE));
      IRShape s; s.stretch = 0.5; s.fade_out = 0;
      auto ir = ReverbIR::make({c}, RATE, RATE, s, &err, LongConvolver::Inline, HQ);
      double db = 0;   // 1 kHz plays at 2 kHz; an alias of 15 kHz would be at 22050 - 15000 = 7050 Hz, playing at 14.1 kHz
      if (ir) { auto y = impulse_response(*ir, ir->frames); db = 20 * std::log10(amp(y, 14100, RATE, 500, 6000) / amp(y, 2000, RATE, 500, 6000) + 1e-12); }
      CHECK(ir && db < -60, "Stretch 50%%: no alias of a 15 kHz partial (%.0f dB under the kept one)", db);
    }
  }
  { // the playhead's note detection (onset.h): hits, plucks and pads with slow attacks over long releases; a held,
    // beating chord or tremolo doesn't keep restarting it
    const double R2 = 48000;
    struct Nt { double t, f, att, rel, len, amp; };
    auto render = [&](const std::vector<Nt>& ns, double secs, bool detune, double trem) {
      std::vector<float> x((size_t)(secs * R2), 0.f);
      for (auto& n : ns)
        for (size_t i = (size_t)(n.t * R2); i < x.size(); i++) {
          const double t = i / R2 - n.t;
          double env = t < n.att ? t / n.att : 1.0;
          if (t > n.len) env *= std::exp(-(t - n.len) / (n.rel / 6.9));
          if (env < 1e-4 && t > n.len) break;
          double v = 0;
          if (detune) { for (double dt : {-0.006, 0.0, 0.0071}) for (int h = 1; h <= 8; h++) v += 0.6 * std::sin(2 * M_PI * n.f * (1 + dt) * h * t + h * (1 + dt * 300)) / h; }
          else for (int h = 1; h <= 5; h++) v += std::sin(2 * M_PI * n.f * h * t + h) / h;
          x[i] += (float)(n.amp * env * v);
        }
      if (trem > 0) for (size_t i = 0; i < x.size(); i++) x[i] *= (float)(1 + trem * std::sin(2 * M_PI * 5 * i / R2));
      return x;
    };
    auto count = [&](const std::vector<Nt>& ns, const std::vector<float>& x, int& caught, int& extra) {
      OnsetDetector d(R2); std::vector<double> on;
      for (size_t p = 0; p + 128 <= x.size(); p += 128) if (d.process(&x[p], &x[p], 128)) on.push_back(p / R2);
      caught = extra = 0;
      for (auto& n : ns) { for (double t : on) if (t >= n.t - 0.01 && t <= n.t + std::max(0.15, n.att + 0.05)) { caught++; break; } }
      for (double t : on) { bool m = false; for (auto& n : ns) if (t >= n.t - 0.01 && t <= n.t + std::max(0.15, n.att + 0.05)) m = true; if (!m) extra++; }
      return on.size();
    };
    const double fs[] = {220, 277, 330, 247, 196, 262, 294, 349};
    std::vector<Nt> keys, pad, pad2;
    for (int i = 0; i < 16; i++) keys.push_back({0.5 + i * 0.4, fs[i % 8], 0.005, 0.8, 0.3, 0.2});
    for (int i = 0; i < 8; i++) pad.push_back({0.5 + i * 1.5, fs[i % 8], 0.35, 2.5, 1.2, 0.15});
    for (int i = 0; i < 8; i++) pad2.push_back({0.5 + i * 1.2, fs[i % 8], 0.8, 3.0, 1.0, 0.15});
    int c1, e1, c2, e2, c3, e3, c4, e4, c5, e5;
    count(keys, render(keys, 8, false, 0), c1, e1);
    count(pad, render(pad, 14, false, 0), c2, e2);
    count(pad2, render(pad2, 13, false, 0), c3, e3);
    count(pad, render(pad, 14, true, 0), c4, e4);
    const std::vector<Nt> held = {{0.3, 220, 0.3, 1, 30, 0.15}, {0.3, 277, 0.3, 1, 30, 0.15}, {0.3, 330, 0.3, 1, 30, 0.15}};
    const size_t held_n = count(held, render(held, 12, true, 0), c5, e5);
    std::vector<Nt> one = {{0.3, 220, 0.3, 1, 30, 0.2}}; int c6, e6;
    const size_t trem_n = count(one, render(one, 12, false, 0.35), c6, e6);
    CHECK(c1 == 16 && e1 == 0 && c2 >= 7 && e2 == 0 && c3 >= 6 && e3 == 0 && c4 == 8 && e4 <= 1 && held_n <= 2 && trem_n == 1,
          "notes heard: plucks %d/16, pads (350 ms attack) %d/8, (800 ms) %d/8, detuned pads %d/8 (%d extra); a held detuned chord %zu, tremolo %zu trigger(s)",
          c1, c2, c3, c4, e4, held_n, trem_n);
  }
  { // knob moves don't click: Output -24 -> +12 dB and Pre-delay 0 -> 250 ms between two blocks of a steady tone
    auto worst = [&](int which) {
      ReverbEngine e(RATE); e.params.mix = 0.5f;
      e.set_ir(ir_of({decay(22050, 3, 0.2f)}));
      const size_t n = 128 * 400; std::vector<float> x(n), ol(n), orr(n);
      for (size_t i = 0; i < n; i++) x[i] = 0.5f * (float)std::sin(2 * M_PI * 220 * i / RATE);
      if (which == 0) e.params.output_db = -24;
      for (size_t p = 0; p < n; p += 128) {
        if (p == 128 * 300) { if (which == 0) e.params.output_db = 12; else e.params.predelay_ms = 250; }
        e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      }
      double d = 0, dref = 0;
      for (size_t i = 128 * 300 - 64; i < 128 * 300 + 256; i++) d = std::max(d, (double)std::fabs(ol[i] - ol[i - 1]));
      for (size_t i = 128 * 380; i < n; i++) dref = std::max(dref, (double)std::fabs(ol[i] - ol[i - 1]));
      return d / std::max(1e-9, dref);
    };
    const double g = worst(0), pd = worst(1);
    CHECK(g < 1.2 && pd < 1.5, "Output and Pre-delay changes glide (largest step vs the steady signal's: %.2f, %.2f)", g, pd);
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
  { // Duck: the wet signal turned down while the input plays, back in the gaps; the feedback loop untouched
    const size_t N = (size_t)(4.0 * RATE), on = (size_t)(1.5 * RATE);   // 1.5 s of noise at -12 dBFS, then silence
    std::mt19937 rn(5); std::normal_distribution<float> gn(0, 1);
    std::vector<float> xl(N, 0.0f), xr(N, 0.0f);
    for (size_t i = 0; i < on; i++) { xl[i] = 0.25f * 0.5f * gn(rn); xr[i] = 0.25f * 0.5f * gn(rn); }
    auto room = [&] { return ir_of({decay((size_t)(1.5 * RATE), 31, 0.3f), decay((size_t)(1.5 * RATE), 32, 0.3f)}); };
    auto render = [&](float duck, float fb, int mode) {        // mode 0: duck untouched, 1: set
      ReverbEngine e(RATE); e.params.mix = 1; e.params.feedback = fb;
      if (mode == 1) e.params.duck = duck;
      e.set_ir(room());
      std::vector<float> ol, orr; run(e, xl, xr, ol, orr); return ol;
    };
    auto seg = [](const std::vector<float>& v, double a, double b) {
      return std::vector<float>(v.begin() + (size_t)(a * RATE), v.begin() + (size_t)(b * RATE)); };
    const auto base = render(0, 0, 0), zero = render(0, 0, 1), full = render(1, 0, 1), half = render(0.5f, 0, 1);
    CHECK(base == zero, "Duck 0: the output bit for bit as without Duck");
    const double during = 20 * std::log10(rms(seg(full, 0.8, 1.4)) / rms(seg(base, 0.8, 1.4))),
                 during_half = 20 * std::log10(rms(seg(half, 0.8, 1.4)) / rms(seg(base, 0.8, 1.4)));
    CHECK(during < -21 && during > -25 && during_half < -10 && during_half > -14,
          "a steady loud input: the reverb %.1f dB at Duck 100%%, %.1f dB at 50%%", during, during_half);
    const double gap = 20 * std::log10(rms(seg(full, 1.9, 2.3)) / rms(seg(base, 1.9, 2.3)));
    CHECK(gap > -1.0, "0.4 s after the input stops the tail is back (%.2f dB)", gap);
    const auto fb0 = render(0, 0.6f, 1), fb1 = render(1, 0.6f, 1);
    double late = 0, late_ref = 0;
    for (size_t i = (size_t)(3.0 * RATE); i < N; i++) { late += std::fabs(fb1[i] - fb0[i]); late_ref += std::fabs(fb0[i]); }
    CHECK(late_ref > 0 && late / late_ref < 1e-3, "with Feedback 60%%, the tail after the input (3-4 s) is as without Duck (diff %.1e)",
          late_ref > 0 ? late / late_ref : -1.0);
  }
  {   // FREEZE: the reverb as it sounds is held as a pad; new playing doesn't reach it; let go, it fades away
    auto room = [] { return ir_of({decay((size_t)(2.0 * RATE), 7, 0.1f), decay((size_t)(2.0 * RATE), 8, 0.1f)}); };
    auto at = [](double s) { return (size_t)(s * RATE); };
    auto win = [&](const std::vector<float>& v, double a, double b) {
      double e = 0; for (size_t i = at(a); i < at(b); i++) e += (double)v[i] * v[i]; return std::sqrt(e / (at(b) - at(a))); };
    auto db = [](double a, double b) { return 20 * std::log10(std::max(1e-12, a) / std::max(1e-12, b)); };
    // a scripted run: input x, freeze on/off at times (seconds), mix 1 (only the reverb is heard)
    auto play = [&](std::vector<float> x, std::vector<std::pair<double, bool>> events, ReverbEngine* keep = nullptr) {
      x.resize((x.size() + 127) / 128 * 128, 0.0f);       // (whole blocks)
      ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000;
      e.set_ir(room());
      std::vector<float> ol(x.size(), 0), orr(x.size(), 0);
      size_t k = 0;
      for (size_t p = 0; p < x.size(); p += 128) {
        while (k < events.size() && at(events[k].first) <= p) e.params.freeze = events[k++].second;
        e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      }
      (void)keep;
      return std::make_pair(ol, orr);
    };
    std::mt19937 rg(3); std::normal_distribution<float> g(0, 0.2f);
    std::vector<float> burst(at(14), 0.0f);
    for (size_t i = 0; i < at(1.0); i++) burst[i] = g(rg);                  // 1 s of noise, then silence
    const auto plain = play(burst, {}).first;
    const auto held = play(burst, {{1.1, true}}).first;
    const double before = win(plain, 1.0, 1.1), early = win(held, 2.0, 3.0), late = win(held, 12.0, 13.0);
    CHECK(std::fabs(db(early, before)) < 3.0 && std::fabs(db(late, early)) < 1.0 && win(plain, 12, 13) < 1e-4,
          "FREEZE holds the tail at its level: %+.1f dB of the moment it's pressed, %+.2f dB 10 s later (unfrozen: gone)",
          db(early, before), db(late, early));
    bool finite = true; float pk = 0;
    for (float v : held) { finite = finite && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
    CHECK(finite && pk < 8 * early, "the pad is finite and steady (peak %.2f x its rms)", pk / std::max(1e-12, early));
    std::vector<float> more = burst;                                        // frozen, play again: it isn't heard in the reverb
    for (size_t i = at(5); i < at(7); i++) more[i] = 3 * g(rg);
    const auto held2 = play(more, {{1.1, true}}).first;
    CHECK(std::fabs(db(win(held2, 5.5, 7.5), early)) < 1.0, "while frozen, new playing doesn't reach the reverb (%+.2f dB)", db(win(held2, 5.5, 7.5), early));
    const auto rel = play(burst, {{1.1, true}, {4.0, false}}).first;
    CHECK(db(win(rel, 7.5, 8.5), early) < -50 && db(win(rel, 4.2, 4.4), early) > -3,
          "let go, the pad fades away (%.1f dB after 0.3 s, %.0f dB after 4 s)", db(win(rel, 4.2, 4.4), early), db(win(rel, 7.5, 8.5), early));
    std::vector<float> after = burst;                                       // let go and play: the reverb is back
    for (size_t i = at(9); i < at(10); i++) after[i] = g(rg);
    const auto back = play(after, {{1.1, true}, {4.0, false}}).first;
    CHECK(std::fabs(db(win(back, 9.5, 10.0), win(plain, 0.5, 1.0))) < 2.0, "...and the reverb is back for what's played next (%+.2f dB)",
          db(win(back, 9.5, 10.0), win(plain, 0.5, 1.0)));
    const auto again = play(burst, {{1.1, true}, {4.0, false}, {4.3, true}}).first;
    CHECK(std::fabs(db(win(again, 5.5, 6.5), early)) < 3.0, "frozen again while it fades: held at about the level heard (%+.2f dB)",
          db(win(again, 5.5, 6.5), early));
    std::vector<float> none(at(4), 0.0f);
    const auto quiet = play(none, {{0.5, true}}).first;
    CHECK(win(quiet, 1, 4) < 1e-7, "frozen in silence: silence (%.1e)", win(quiet, 1, 4));
    std::vector<float> tone(at(8), 0.0f);                                   // the pitch of what was playing is kept
    for (size_t i = 0; i < at(1.0); i++) tone[i] = 0.3f * std::sin(2 * M_PI * 440.0 * i / RATE);
    const auto tz = play(tone, {{0.9, true}}).first;
    double in = 0, all = 0;
    {
      const int N = 16384; std::vector<float> seg(tz.begin() + at(4), tz.begin() + at(4) + N);
      for (int k = 20; k < N / 2; k++) {                                     // a plain DFT of the bins (test only)
        double re = 0, im = 0;
        if (k % 4) continue;
        for (int i = 0; i < N; i++) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / N); re += w * seg[i] * std::cos(2 * M_PI * k * i / N); im -= w * seg[i] * std::sin(2 * M_PI * k * i / N); }
        const double p = re * re + im * im, f = k * RATE / N;
        all += p; if (f > 400 && f < 480) in += p;
      }
    }
    CHECK(all > 0 && in / all > 0.8, "the frozen pad keeps the note's pitch (%.0f%% of its energy at 400-480 Hz)", 100 * in / std::max(1e-30, all));
    const auto st = play(burst, {{1.1, true}});
    double c = 0, a2 = 0, b2 = 0;
    for (size_t i = at(3); i < at(5); i++) { c += (double)st.first[i] * st.second[i]; a2 += (double)st.first[i] * st.first[i]; b2 += (double)st.second[i] * st.second[i]; }
    CHECK(std::fabs(c / std::sqrt(a2 * b2)) < 0.3, "a stereo pad (L/R correlation %.2f)", c / std::sqrt(a2 * b2));
  }
  {   // END SYNC: the IR kept for a note's length from Start
    const auto room4 = std::vector<float>(decay((size_t)(4.0 * RATE), 9, 0.1f));
    IRShape s; s.length_s = 0.5;
    auto a = ir_shaped({room4}, s);
    s.start = 0.25; auto b = ir_shaped({room4}, s);
    s.start = 0; s.length_s = 6.0; auto c = ir_shaped({room4}, s);
    CHECK(a && b && c && a->frames == (size_t)(0.5 * RATE) && b->frames == (size_t)(0.5 * RATE) && c->frames == (size_t)(4.0 * RATE),
          "END SYNC: 0.5 s kept (%zu frames, from 25%%: %zu); longer than the IR: all of it (%zu)", a ? a->frames : 0, b ? b->frames : 0, c ? c->frames : 0);
  }
  {   // DAMPING and DISTANCE: the IR reshaped in its own time
    const std::vector<float> hall = decay((size_t)(2.5 * RATE), 11, 0.08f);
    auto band = [](const std::vector<float>& v, size_t a, size_t b) {   // highs / all, by the first difference
      double e = 0, d = 0; for (size_t i = a + 1; i < b && i < v.size(); i++) { e += (double)v[i] * v[i]; d += (double)(v[i] - v[i - 1]) * (v[i] - v[i - 1]); }
      return std::make_pair(e, d / std::max(1e-30, e)); };
    auto db = [](double r) { return 10 * std::log10(std::max(1e-30, r)); };
    const size_t NR = (size_t)(2.6 * RATE) / 128 * 128;
    auto resp = [&](const IRShape& s) { return impulse_response(ir_shaped({hall}, s), NR); };
    IRShape s0; const auto P = resp(s0);
    IRShape sd; sd.damping = 1.0; const auto D = resp(sd);
    IRShape sh; sh.damping = 0.5; const auto Hh = resp(sh);
    const size_t e0 = (size_t)(0.03 * RATE), l0 = (size_t)(1.0 * RATE), l1 = (size_t)(1.5 * RATE);
    const double early = db(band(D, 0, e0).second / band(P, 0, e0).second), late = db(band(D, l0, l1).second / band(P, l0, l1).second),
                 lateh = db(band(Hh, l0, l1).second / band(P, l0, l1).second);
    CHECK(std::fabs(early) < 2.0 && late < -15 && lateh < -3 && lateh > late,
          "DAMPING: the start keeps its highs (%+.1f dB), the tail at 1-1.5 s loses them (%.0f dB at 100%%, %.0f dB at 50%%)", early, late, lateh);
    double pkd = 0; bool fin = true; for (float v : D) { pkd = std::max(pkd, (double)std::fabs(v)); fin = fin && std::isfinite(v); }
    CHECK(fin && pkd > 0, "DAMPING: finite");
    // DISTANCE: the early part (80 ms from the onset) against the tail
    auto er = [&](const std::vector<float>& v) { return db(band(v, 0, (size_t)(0.07 * RATE)).first / band(v, (size_t)(0.1 * RATE), v.size()).first); };
    IRShape sn; sn.distance = 0.0; const auto Nr = resp(sn);
    IRShape sf; sf.distance = 1.0; const auto Fr = resp(sf);
    IRShape sm; sm.distance = 0.5; const auto Mr = resp(sm);
    const double rp = er(P), rn = er(Nr), rf = er(Fr);
    CHECK(rn - rp > 10 && rp - rf > 10, "DISTANCE: early / tail %+.1f dB near, %+.1f as recorded, %+.1f far", rn, rp, rf);
    CHECK(band(Fr, 0, (size_t)(0.07 * RATE)).second < 0.5 * band(P, 0, (size_t)(0.07 * RATE)).second,
          "DISTANCE far: the early part is darker");
    bool same = Mr.size() == P.size();
    for (size_t i = 0; same && i < P.size(); i++) same = Mr[i] == P[i];
    CHECK(same, "DISTANCE 50%% and DAMPING 0 leave the IR exactly as it was");
  }
  {   // SCAN: the reverb's last seconds, read back from a point you move
    auto at = [](double s) { return (size_t)(s * RATE) / 128 * 128; };
    auto win = [&](const std::vector<float>& v, double a, double b) {
      double e = 0; for (size_t i = at(a); i < at(b); i++) e += (double)v[i] * v[i]; return std::sqrt(e / std::max<size_t>(1, at(b) - at(a))); };
    auto db = [](double a, double b) { return 20 * std::log10(std::max(1e-12, a) / std::max(1e-12, b)); };
    struct Ev { double t; bool on; float pos, tex, mix; };
    auto run = [&](const std::vector<float>& x, std::vector<Ev> ev, int* ph = nullptr) {
      ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000;
      e.set_ir(ir_of({decay((size_t)(2.0 * RATE), 21, 0.1f), decay((size_t)(2.0 * RATE), 22, 0.1f)}));
      std::vector<float> ol(x.size(), 0), orr(x.size(), 0);
      size_t k = 0;
      for (size_t p = 0; p + 128 <= x.size(); p += 128) {
        while (k < ev.size() && at(ev[k].t) <= p) { e.params.scan = ev[k].on; e.params.scan_pos = ev[k].pos; e.params.scan_tex = ev[k].tex; e.params.scan_mix = ev[k].mix; k++; }
        e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
        if (ph) *ph = e.playhead();
      }
      return ol;
    };
    std::mt19937 rg(9); std::normal_distribution<float> g(0, 0.2f);
    std::vector<float> noise(at(6)); for (auto& v : noise) v = g(rg);
    const auto plain = run(noise, {});
    const auto offmoved = run(noise, {{0.5, false, 0.7f, 0.9f, 0.5f}});
    CHECK(plain == offmoved, "SCAN off: the sound is exactly as without it (whatever its knobs)");
    for (float tex : {0.0f, 0.5f, 1.0f}) {
      const auto s = run(noise, {{2.0, true, 0.3f, tex, 1.0f}});
      bool fin = true; for (float v : s) fin = fin && std::isfinite(v);
      CHECK(fin && std::fabs(db(win(s, 3, 5.5), win(plain, 3, 5.5))) < 2.0, "SCAN on, texture %.0f%%: the level kept (%+.1f dB)", 100 * tex, db(win(s, 3, 5.5), win(plain, 3, 5.5)));
    }
    std::vector<float> burst(at(4), 0.0f); for (size_t i = 0; i < at(1.0); i++) burst[i] = g(rg);
    const auto near = run(burst, {{2.0, true, 0.0f, 0.0f, 1.0f}}), far = run(burst, {{2.0, true, 1.0f, 0.0f, 1.0f}});
    CHECK(db(win(far, 2.3, 2.6), win(near, 2.3, 2.6)) > 12, "POSITION: far back reads the loud start of the tail (%+.0f dB over near)", db(win(far, 2.3, 2.6), win(near, 2.3, 2.6)));
    std::vector<float> tone(at(6)); for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.3f * std::sin(2 * M_PI * 440.0 * i / RATE);
    auto share = [&](const std::vector<float>& v, double lo, double hi) {   // energy share in [lo, hi) Hz (a plain DFT, every 4th bin)
      const int N = 16384; double in = 0, all = 0;
      for (int k = 20; k < N / 2; k += 4) {
        double re = 0, im = 0;
        for (int i = 0; i < N; i++) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / N); const double x = w * v[at(4) + i]; re += x * std::cos(2 * M_PI * k * i / N); im -= x * std::sin(2 * M_PI * k * i / N); }
        const double p = re * re + im * im, f = k * RATE / N; all += p; if (f >= lo && f < hi) in += p;
      }
      return all > 0 ? in / all : 0.0;
    };
    const double s0 = share(run(tone, {{1.0, true, 0.2f, 0.0f, 1.0f}}), 400, 480), s5 = share(run(tone, {{1.0, true, 0.2f, 0.55f, 1.0f}}), 400, 480),
                 s1 = share(run(tone, {{1.0, true, 0.2f, 1.0f, 1.0f}}), 400, 480);
    CHECK(s0 > 0.8 && s5 < s0 - 0.1 && s1 < 0.5, "TEXTURE: the note kept at 0 (%.0f%% at 440 Hz), with octave/fifth halos at 55%% (%.0f%%), blown into wind at 100%% (%.0f%%)",
          100 * s0, 100 * s5, 100 * s1);
    int ph = -1; run(noise, {{0.5, true, 0.5f, 0.0f, 1.0f}}, &ph);
    CHECK(ph == 1 + (int)std::lround(0.5 * (ReverbEngine::kPlay - 1)), "the glow shows where it reads (%d)", ph);
    const auto half = run(noise, {{2.0, true, 0.3f, 0.5f, 0.5f}});
    CHECK(std::fabs(db(win(half, 3, 5.5), win(plain, 3, 5.5))) < 3.0, "AMOUNT 50%%: reverb and scan together, the level kept (%+.1f dB)", db(win(half, 3, 5.5), win(plain, 3, 5.5)));
    const auto offagain = run(noise, {{1.0, true, 0.3f, 0.5f, 1.0f}, {3.0, false, 0.3f, 0.5f, 1.0f}});
    CHECK(std::fabs(db(win(offagain, 3.5, 5.5), win(plain, 3.5, 5.5))) < 0.5, "SCAN off again: the reverb as it was (%+.2f dB)", db(win(offagain, 3.5, 5.5), win(plain, 3.5, 5.5)));
    // PITCH (both modes) and GRAINS (grains of the input, into the IR)
    auto runm = [&](const std::vector<float>& x, int mode, float pitch, float size, float spray, float dens, double on_at, float direct = 0.0f) {
      ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000;
      e.set_ir(ir_of({decay((size_t)(2.0 * RATE), 21, 0.1f), decay((size_t)(2.0 * RATE), 22, 0.1f)}));
      e.params.scan_mode = mode; e.params.scan_pitch = pitch; e.params.grain_size = size; e.params.grain_spray = spray;
      e.params.grain_density = dens; e.params.grain_direct = direct; e.params.scan_pos = 0.2f; e.params.scan_mix = 1.0f;
      std::vector<float> ol(x.size(), 0), orr(x.size(), 0);
      for (size_t p = 0; p + 128 <= x.size(); p += 128) {
        e.params.scan = p >= at(on_at);
        e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      }
      return ol;
    };
    const auto offg = runm(noise, 1, 7.0f, 0.3f, 0.5f, 1.0f, 99);
    CHECK(offg == plain, "GRAINS chosen but SCAN off: the sound is exactly as without it");
    for (float dens : {0.0f, 0.6f, 1.0f})
      for (float size : {0.0f, 0.6f, 1.0f}) {
        const auto s = runm(noise, 1, 0.0f, size, 0.3f, dens, 1.0);
        bool fin = true; for (float v : s) fin = fin && std::isfinite(v);
        const double d = db(win(s, 3, 5.5), win(plain, 3, 5.5));
        CHECK(fin && d < 2.0 && d > (dens < 0.2f ? -9.0 : -3.0), "GRAINS size %.0f%%, density %.0f%%: the level kept (%+.1f dB)", 100 * size, 100 * dens, d);
      }
    {                                                     // DIRECT: grains past the IR, at about the level they have through it
      const auto ir0 = runm(noise, 1, 0.0f, 0.3f, 0.3f, 0.3f, 1.0, 0.0f), half = runm(noise, 1, 0.0f, 0.3f, 0.3f, 0.3f, 1.0, 0.5f),
                 all = runm(noise, 1, 0.0f, 0.3f, 0.3f, 0.3f, 1.0, 1.0f);
      bool fin = true; for (float v : all) fin = fin && std::isfinite(v);
      const double dh = db(win(half, 3, 5.5), win(ir0, 3, 5.5)), da = db(win(all, 3, 5.5), win(ir0, 3, 5.5));
      // with no IR on them, the grains' gaps stay: the level swings more over 10 ms than through the IR
      auto swing = [&](const std::vector<float>& v) {
        double lo = 1e30, hi = 0;
        for (size_t p = at(3); p + 441 < at(5.5); p += 441) { double e = 0; for (size_t i = p; i < p + 441; i++) e += (double)v[i] * v[i]; lo = std::min(lo, e); hi = std::max(hi, e); }
        return 10 * std::log10(hi / std::max(lo, 1e-20));
      };
      CHECK(fin && std::fabs(dh) < 2.5 && std::fabs(da) < 3.0 && swing(all) > swing(ir0) + 3.0,
            "GRAINS DIRECT 50%% / 100%%: the level kept (%+.1f / %+.1f dB), the grains' edges heard (10 ms swing %.0f vs %.0f dB through the IR)",
            dh, da, swing(all), swing(ir0));
    }
    for (float air : {0.5f, 1.0f}) {                      // AIR: the level kept, nothing runs away (its feedback)
      ReverbEngine e(RATE); e.params.mix = 1; e.params.lowcut_hz = 10; e.params.highcut_hz = 20000;
      e.set_ir(ir_of({decay((size_t)(2.0 * RATE), 21, 0.1f), decay((size_t)(2.0 * RATE), 22, 0.1f)}));
      e.params.scan_mode = 1; e.params.grain_air = air; e.params.scan_pos = 0.2f; e.params.scan_mix = 1.0f;
      std::vector<float> ol(noise.size(), 0), orr(noise.size(), 0);
      for (size_t p = 0; p + 128 <= noise.size(); p += 128) { e.params.scan = p >= at(1.0); e.process(&noise[p], &noise[p], &ol[p], &orr[p], 128); }
      float pk = 0; bool fin = true; for (float v : ol) { fin = fin && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
      const double d = db(win(ol, 3, 5.5), win(plain, 3, 5.5));
      CHECK(fin && std::fabs(d) < 3.0 && pk < 4.0f, "GRAINS AIR %.0f%%: the level kept (%+.1f dB), peak %.2f", 100 * air, d, pk);
    }
    {                                                     // the stripes under the painting: one per grain, true
      ReverbEngine e(RATE); e.params.mix = 1;
      e.set_ir(ir_of({decay((size_t)(2.0 * RATE), 21, 0.1f), decay((size_t)(2.0 * RATE), 22, 0.1f)}));
      e.params.scan_mode = 1; e.params.scan_mix = 1.0f; e.params.grain_spray = 0.0f; e.params.scan = true;
      std::vector<float> ol(128), orr(128);
      int lit_at_start = -1, lit_at_end = -1, most = 0;
      for (size_t p = 0; p + 128 <= tone.size(); p += 128) {
        const double t = (double)p / RATE;
        e.params.scan_pos = (float)std::min(1.0, std::max(0.0, (t - 1.0) / 4.0));   // swept across in 4 s
        e.process(&tone[p], &tone[p], ol.data(), orr.data(), 128);
        int n = 0, first = -1;
        for (int c = 0; c < ReverbEngine::kSparkCols; c++) if (e.spark(c)) { n++; if (first < 0) first = c; }
        most = std::max(most, n);
        if (std::fabs(t - 1.6) < 0.002) lit_at_start = first;
        if (std::fabs(t - 4.9) < 0.002) lit_at_end = first;
      }
      // no Spray: the grains sit around Position (+-a grain's length), so at most a couple of columns show
      CHECK(lit_at_start >= 0 && lit_at_start <= 2 && lit_at_end >= 9 && most <= 3,
            "the stripes follow Position across (column %d -> %d), one per column, at most %d at once with no Spray",
            lit_at_start, lit_at_end, most);
    }
    {                                                     // SYNC: grains on the host's grid
      auto run_sync = [&](double beats, double ppq0, double loop_at, double loop_len, bool play, double secs) {
        ReverbEngine e(RATE); e.params.mix = 1;
        e.set_ir(ir_of({decay((size_t)(1.0 * RATE), 21, 0.1f), decay((size_t)(1.0 * RATE), 22, 0.1f)}));
        e.params.scan_mode = 1; e.params.scan_mix = 1.0f; e.params.scan = true; e.params.grain_beats = (float)beats;
        e.params.grain_size = 0.3f;
        std::vector<float> ol(128), orr(128);
        const double bpm = 120.0, blk = 128 * bpm / 60.0 / RATE;
        double ppq = ppq0;
        long before = -1;
        for (size_t p = 0; p + 128 <= (size_t)(secs * RATE) && p + 128 <= tone.size(); p += 128) {
          if (p == (size_t)(0.3 * RATE) / 128 * 128) before = e.grains_spawned();   // (after SCAN's fade-in)
          e.set_transport(ppq, bpm, play);
          e.process(&tone[p], &tone[p], ol.data(), orr.data(), 128);
          ppq += blk;
          if (loop_len > 0 && ppq >= loop_at) ppq -= loop_len;
        }
        return e.grains_spawned() - before;
      };
      // 120 bpm, 1/8 (0.5 quarter): 4 a second. 5.0 s, counted from 0.3 s: 4.7 s -> 18 or 19
      const long free8 = run_sync(0.5, 3.3, 0, 0, true, 5.0), stop8 = run_sync(0.5, 0, 0, 0, false, 5.0),
                 t16 = run_sync(1.0 / 6, 0, 0, 0, true, 5.0), loop = run_sync(0.5, 0.0, 3.0, 3.0, true, 5.0);
      CHECK(free8 >= 18 && free8 <= 19 && stop8 >= 18 && stop8 <= 19 && t16 >= 55 && t16 <= 57 && loop >= 18 && loop <= 19,
            "SYNC at 120: 1/8 = %ld grains in 4.7 s playing (from ppq 3.3), %ld stopped; 1/16 T = %ld; through a 3-beat loop %ld",
            free8, stop8, t16, loop);
    }
    auto peak_hz = [&](const std::vector<float>& v) {   // the strongest frequency 100..2000 Hz (a DFT, 2 Hz steps)
      const int N = 22050; double best = 0, bf = 0;
      for (double f = 100; f < 2000; f += 2) {
        double re = 0, im = 0;
        for (int i = 0; i < N; i++) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / N); const double x = w * v[at(4) + i]; re += x * std::cos(2 * M_PI * f * i / RATE); im -= x * std::sin(2 * M_PI * f * i / RATE); }
        if (re * re + im * im > best) { best = re * re + im * im; bf = f; }
      }
      return bf;
    };
    const double gu = peak_hz(runm(tone, 1, 12.0f, 0.6f, 0.1f, 0.6f, 1.0)), gd = peak_hz(runm(tone, 1, -12.0f, 0.6f, 0.1f, 0.6f, 1.0)),
                 g7 = peak_hz(runm(tone, 1, 7.0f, 0.6f, 0.1f, 0.6f, 1.0)), pd = peak_hz(runm(tone, 0, -12.0f, 0, 0, 0, 1.0));
    const double gf = peak_hz(runm(tone, 1, 7.5f, 0.6f, 0.1f, 0.6f, 1.0));
    CHECK(std::fabs(gf - 440 * std::pow(2.0, 7.5 / 12)) < 6, "PITCH between semitones: +7.5 moves 440 Hz to %.0f Hz (%.0f)", gf, 440 * std::pow(2.0, 7.5 / 12));
    CHECK(std::fabs(gu - 880) < 6 && std::fabs(gd - 220) < 4 && std::fabs(g7 - 659.3) < 6 && std::fabs(pd - 220) < 4,
          "PITCH: GRAINS +12 / -12 / +7 move 440 Hz to %.0f / %.0f / %.0f Hz; PAD -12 to %.0f Hz", gu, gd, g7, pd);
  }
  { // BEND: the wet pitched as it plays, gliding; back at 0 the wet is untouched again
    std::vector<float> unit(64, 0.0f); unit[0] = 1.0f;       // an IR that passes the input through: wet = input
    const size_t n = 128 * 1400;                             // 4.06 s
    std::vector<float> tone(n);
    for (size_t i = 0; i < n; i++) tone[i] = 0.5f * (float)std::sin(2 * M_PI * 440.0 * i / RATE);
    auto zc_hz = [&](const std::vector<float>& y, size_t a, size_t b) {   // the frequency from zero crossings
      int z = 0; for (size_t i = a + 1; i < b; i++) if ((y[i - 1] < 0) != (y[i] < 0)) z++;
      return z * RATE / 2.0 / (double)(b - a);
    };
    ReverbEngine e(RATE); e.params.mix = 1; e.set_ir(ir_of({unit}));
    std::vector<float> ol(n), orr(n);
    for (size_t p = 0; p < n; p += 128) {
      const double t = p / RATE;                              // 0..0.5 s: 0; then +12 held; at 1.5 s a sweep to -12 over 1 s; at 3 s back to 0
      e.params.bend = t < 0.5 ? 0.0f : t < 1.5 ? 12.0f : t < 2.5 ? (float)(12.0 - 24.0 * (t - 1.5)) : t < 3.0 ? -12.0f : 0.0f;
      e.process(&tone[p], &tone[p], &ol[p], &orr[p], 128);
    }
    const double up = zc_hz(ol, (size_t)(1.0 * RATE), (size_t)(1.45 * RATE)), dn = zc_hz(ol, (size_t)(2.6 * RATE), (size_t)(2.95 * RATE));
    float jump = 0; for (size_t i = (size_t)(0.6 * RATE); i < (size_t)(3.0 * RATE); i++) jump = std::max(jump, std::fabs(ol[i] - ol[i - 1]));
    const float sine_step = 0.5f * (float)(2 * M_PI * 880.0 / RATE);   // the steepest step of the +12 tone itself
    ReverbEngine ref(RATE); ref.params.mix = 1; ref.set_ir(ir_of({unit}));   // the same, never bent
    std::vector<float> rl(n), rr(n);
    for (size_t p = 0; p < n; p += 128) ref.process(&tone[p], &tone[p], &rl[p], &rr[p], 128);
    double back = 0; for (size_t i = (size_t)(3.5 * RATE); i < n; i++) back = std::max(back, (double)std::fabs(ol[i] - rl[i]));
    CHECK(std::fabs(up - 880) < 16 && std::fabs(dn - 220) < 6, "BEND +12 / -12: 440 Hz comes out at %.0f / %.0f Hz", up, dn);
    CHECK(jump < 1.6f * sine_step, "BEND glides: no step while it holds and sweeps (largest %.3f, a +12 tone's own %.3f)", jump, sine_step);
    CHECK(back < 1e-6, "BEND back at 0: the wet is untouched again (max diff %.1e)", back);
  }
  { // SHIMMER: an octave up fed back: the octaves build; bounded with Feedback at 100% too; it dies away after the playing
    auto bin = [](const std::vector<float>& y, size_t a, size_t b, double f) {   // Goertzel: the level at f
      const double c = 2 * std::cos(2 * M_PI * f / RATE); double s1 = 0, s2 = 0;
      for (size_t i = a; i < b; i++) { const double s0 = y[i] + c * s1 - s2; s2 = s1; s1 = s0; }
      return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double)(b - a);
    };
    double oct[2] = {0, 0}, peak = 0, late = 1, playing = 0; bool finite = true;
    for (int k = 0; k < 3; k++) {
      ReverbEngine e(RATE); e.params.mix = 1; e.params.shimmer = k ? 1.0f : 0.0f; e.params.feedback = k == 2 ? 1.0f : 0.0f;
      e.set_ir(ir_of({decay(44100, 7, 0.2f)}));
      const size_t n = (size_t)(RATE * 40) / 128 * 128; std::vector<float> x(n, 0.0f), ol(n), orr(n);
      for (size_t i = 0; i < (size_t)(2 * RATE); i++) x[i] = 0.3f * (float)std::sin(2 * M_PI * 220.0 * i / RATE);
      for (size_t p = 0; p < n; p += 128) e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      if (k < 2) oct[k] = bin(ol, (size_t)RATE, (size_t)(2 * RATE), 440) + bin(ol, (size_t)RATE, (size_t)(2 * RATE), 880);
      if (k == 2) {
        for (size_t i = 0; i < n; i++) { if (!std::isfinite(ol[i])) finite = false; peak = std::max(peak, (double)std::fabs(ol[i])); }
        playing = rms(std::vector<float>(ol.begin() + (size_t)RATE, ol.begin() + (size_t)(2 * RATE)));
        late = rms(std::vector<float>(ol.end() - (size_t)RATE, ol.end()));
      }
    }
    CHECK(oct[1] > 0.05 && oct[0] < 1e-3, "SHIMMER 100%%: a 220 Hz tone's octaves (440 + 880 Hz) build to %.3f (without: %.4f)", oct[1], oct[0]);
    CHECK(finite && peak < 2.0, "SHIMMER 100%% with Feedback 100%%: bounded (peak %.2f)", peak);
    CHECK(late < playing * 1e-3, "SHIMMER 100%% with Feedback 100%%: it ends once the input stops (%.0f dB under after 38 s)", 20 * std::log10(playing / std::max(1e-12, late)));
  }
  { // MOTION and AGE: out of the path at 0; Motion widens a mono tail; Age darkens it; both keep the level and stay smooth
    const size_t n = (size_t)(RATE * 4) / 128 * 128;
    std::vector<float> x(n);
    std::mt19937 rq(5); std::normal_distribution<float> gq(0, 0.1f);
    float lpq = 0; for (size_t i = 0; i < n; i++) { lpq += 0.15f * (gq(rq) - lpq); x[i] = 3 * lpq; }   // (a darker noise: closer to music)
    auto render = [&](float mo, float ag, std::vector<float>& ol, std::vector<float>& orr, bool back = false) {
      ReverbEngine e(RATE); e.params.mix = 1; e.set_ir(ir_of({decay(22050, 3, 0.2f)}));
      ol.assign(n, 0); orr.assign(n, 0);
      for (size_t p = 0; p < n; p += 128) {
        const bool on = !back || p < n / 2;
        e.params.motion = on ? mo : 0.0f; e.params.age = on ? ag : 0.0f;
        e.process(&x[p], &x[p], &ol[p], &orr[p], 128);
      }
    };
    auto bright = [](const std::vector<float>& y, size_t a) { double d = 0, t = 0; for (size_t i = a + 1; i < y.size(); i++) { d += (y[i] - y[i - 1]) * (y[i] - y[i - 1]); t += y[i] * y[i]; } return d / std::max(1e-12, t); };
    std::vector<float> l0, r0, lm, rm, la, ra, lb, rb;
    render(0, 0, l0, r0); render(1, 0, lm, rm); render(0, 1, la, ra); render(1, 1, lb, rb, true);
    double c = 0, a2 = 0, b2 = 0; for (size_t i = n / 4; i < n; i++) { c += lm[i] * rm[i]; a2 += lm[i] * lm[i]; b2 += rm[i] * rm[i]; }
    const double corr = c / std::sqrt(a2 * b2);
    const double lv_m = 20 * std::log10(rms(lm, n / 4) / rms(l0, n / 4)), lv_a = 20 * std::log10(rms(la, n / 4) / rms(l0, n / 4));
    double back = 0; for (size_t i = n / 2 + (size_t)(0.1 * RATE); i < n; i++) back = std::max(back, (double)std::fabs(lb[i] - l0[i]));
    CHECK(corr < 0.9 && std::fabs(lv_m) < 2, "MOTION 100%%: a mono tail moves apart (L/R corr %.2f), level %+.1f dB", corr, lv_m);
    CHECK(bright(la, n / 4) < 0.5 * bright(l0, n / 4) && std::fabs(lv_a) < 3, "AGE 100%%: darker (brightness x%.2f), level %+.1f dB",
          bright(la, n / 4) / bright(l0, n / 4), lv_a);
    CHECK(back < 1e-6, "MOTION and AGE back at 0: the wet is untouched again (max diff %.1e)", back);
  }
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
