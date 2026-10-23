// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// bench_reverb.cpp -- long-IR convolution on MPC's audio clock (44.1 kHz, 128-frame blocks, paced in real time).
//   irrev-bench [-v] [--quick]   IR lengths 1, 2, 3, 5, 10, 20 s, mono and stereo, four methods (2-stage: the tail in
//                                2048-sample blocks only, Gen 1 before 1.3.0; 3-stage: plus the far stage, since 1.3.0), three methods (see long_convolver.h):
//                                audio-thread cost per block (mean / p99 / max, % of 2902 us), the worker thread's
//                                load (% of one core) and missed tail deadlines
//   irrev-bench --check          the convolver against direct convolution (all three methods)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <time.h>
#include <vector>
#include "long_convolver.h"
#include "platform.h"
using namespace irrev;
static const double RATE = 44100;
static const int B = 128;
static double cpu_us() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static double wall_s() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static bool verbose = false;

static std::vector<float> make_ir(size_t n, unsigned seed) {            // a reverb-like IR: noise, decaying
  std::mt19937 r(seed); std::normal_distribution<float> g(0.f, 1.f);
  std::vector<float> h(n);
  for (size_t i = 0; i < n; i++) h[i] = 0.2f * g(r) * std::exp(-6.9f * (float)i / (float)n);   // -60 dB at the end
  return h;
}

// the convolver (all three methods) against direct convolution: irn taps, xn input samples, far = the far stage's block
static int check_one(size_t irn, size_t xn, int far) {
  int fails = 0;
  auto h = make_ir(irn, 3);
  std::mt19937 r(5); std::normal_distribution<float> g(0.f, 0.1f);
  std::vector<float> x(xn); for (auto& v : x) v = g(r);
  std::vector<double> ref(xn, 0.0);                                     // direct convolution, in double
  for (size_t i = 0; i < xn; i++) { double a = 0; for (size_t k = 0; k <= i && k < irn; k++) a += (double)h[k] * x[i - k]; ref[i] = a; }
  double rr = 0; for (double v : ref) rr += v * v; rr = std::sqrt(rr / xn);
  for (auto mode : {LongConvolver::Uniform, LongConvolver::Inline, LongConvolver::Threaded}) {
    LongConvolver c(h, mode, far);
    std::vector<float> y(xn);
    double next = wall_s();
    for (size_t p = 0; p < xn; p += B) {
      c.process(&x[p], &y[p], B);
      if (mode == LongConvolver::Threaded) {                            // real time, so the worker has its deadline
        next += B / RATE;
        double w = next - wall_s();
        if (w > 0) { timespec ts = {0, (long)(w * 1e9)}; nanosleep(&ts, nullptr); }
      }
    }
    double e = 0, mx = 0;
    for (size_t i = 0; i < xn; i++) { double d = y[i] - ref[i]; e += d * d; mx = std::max(mx, std::fabs(d)); }
    e = std::sqrt(e / xn);
    const char* name = mode == LongConvolver::Uniform ? "uniform" : mode == LongConvolver::Inline ? "inline" : "threaded";
    bool ok = e < 1e-5 * rr && c.misses() == 0;
    printf("%s %-8s %6zu taps, %zu stage(s) vs direct convolution: rms error %.1e (%.0f dB below the signal), max %.1e, misses %ld\n",
           ok ? "ok  " : "FAIL", name, irn, c.stages(), e, 20 * std::log10(rr / std::max(e, 1e-30)), mx, c.misses());
    fails += !ok;
  }
  return fails;
}

static int check() {
  int fails = 0;
  fails += check_one(13230 /* 0.3 s: head + tail */, 44160, 0);         // (a whole number of 128-sample blocks)
  fails += check_one(20000, 61440, 6144);   // head + 2048 stage + a far stage (6144: taps from 12288; small, to be quick)
  fails += check_one(40000, 98304, 16384);  // head + 2048 stage + the Gen 2 far stage (16384: taps from 32768)
  printf("%s\n", fails ? "FAILED" : "PASSED");
  return fails ? 1 : 0;
}

struct Result { double mean, p99, mx, worker; long misses; };

static Result run(double secs_ir, int chans, LongConvolver::Mode mode, double run_s, int far = -1) {
  const size_t n = (size_t)(secs_ir * RATE);
  std::vector<std::unique_ptr<LongConvolver>> c;
  for (int ch = 0; ch < chans; ch++) c.emplace_back(new LongConvolver(make_ir(n, 11 + ch), mode, far));
  std::mt19937 r(7); std::normal_distribution<float> g(0.f, 0.1f);
  std::vector<float> in(B), out(B);
  std::vector<double> t;
  const int blocks = (int)(run_s * RATE / B);
  const double budget = 1e6 * B / RATE;
  double next = wall_s();
  for (int b = 0; b < blocks; b++) {
    for (auto& v : in) v = g(r);
    double s = cpu_us();
    for (auto& cv : c) cv->process(in.data(), out.data(), B);
    if (b > 50) t.push_back(cpu_us() - s);
    next += B / RATE;                                                   // MPC's clock: one block every 2.9 ms
    double w = next - wall_s();
    if (w > 0) { timespec ts = {0, (long)(w * 1e9)}; nanosleep(&ts, nullptr); }
  }
  std::sort(t.begin(), t.end());
  double mean = 0; for (double v : t) mean += v; mean /= t.size();
  Result res{100 * mean / budget, 100 * t[t.size() * 99 / 100] / budget, 100 * t.back() / budget, 0, 0};
  for (auto& cv : c) { res.worker += cv->worker_cpu_seconds(); res.misses += cv->misses(); }
  res.worker = 100 * res.worker / run_s;
  return res;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  bool quick = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--check")) return check();
    if (!strcmp(argv[i], "-v")) verbose = true;
    if (!strcmp(argv[i], "--quick")) quick = true;
  }
  const double run_s = quick ? 2.0 : 4.0;
  printf("Liminal Hz convolution on MPC's clock: 44.1 kHz, 128-frame blocks paced in real time (2902 us each).\n");
  printf("audio thread: %% of one block (mean / p99 / max); worker: %% of one core; misses: tail blocks late\n\n");
  printf("%-5s %-6s %-9s %8s %8s %8s %9s %7s\n", "IR", "chans", "method", "mean", "p99", "max", "worker", "misses");
  // methods: uniform (no split), inline (tail on the audio thread), threaded with the 2048 tail only (Gen 1 before 1.3.0)
  // and threaded with the far stage too (both builds since 1.3.0)
  struct M { const char* name; LongConvolver::Mode mode; int far; };
  const M methods[] = {{"uniform", LongConvolver::Uniform, 0}, {"inline", LongConvolver::Inline, 0},
                       {"2-stage", LongConvolver::Threaded, 0}, {"3-stage", LongConvolver::Threaded, platform::kFarBlock}};
  for (double secs : {1.0, 2.0, 3.0, 5.0, 10.0, 20.0})
    for (int ch : {1, 2})
      for (const M& m : methods) {
        if (m.mode == LongConvolver::Uniform && secs * ch > 3.0) {       // would take far longer than real time
          printf("%-5.0fs %-6s %-9s %8s\n", secs, ch == 1 ? "mono" : "stereo", m.name, "(skipped: too heavy)");
          continue;
        }
        if (m.mode == LongConvolver::Inline && secs > 5.0) continue;      // (the point is made at 5 s)
        if (verbose) printf("  running %.0f s %s %s...\n", secs, ch == 1 ? "mono" : "stereo", m.name);
        Result r = run(secs, ch, m.mode, run_s, m.far);
        printf("%-5.0fs %-6s %-9s %7.1f%% %7.1f%% %7.1f%% %8.1f%% %7ld\n", secs, ch == 1 ? "mono" : "stereo", m.name,
               r.mean, r.p99, r.mx, r.worker, r.misses);
      }
  printf("done\n");
  return 0;
}
