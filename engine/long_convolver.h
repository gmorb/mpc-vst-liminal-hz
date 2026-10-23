// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* long_convolver.h -- convolution with long impulse responses (reverbs, seconds long) on a Gen 1 MPC/Force.
 *
 * The IR is split in two:
 *   head  taps [0, 2W):  uniformly partitioned in 128-sample blocks (FFT 256), on the audio thread: zero latency,
 *                        the same cost every block;
 *   tail  taps [2W, N):  uniformly partitioned in W-sample blocks (FFT 2W). Output block m of the tail needs input
 *                        only up to block m-2, which is complete one W-period before block m starts, so the tail
 *                        is computed one block ahead, with a whole W-period (W = 2048: 46 ms) to do it in.
 * A third, far stage for long IRs (platform.h kFarBlock, both builds since 1.3.0): taps [2F, N) in F-sample blocks (F = 16384, its own
 * worker), and the 2048 stage then covers [2W, 2F). Each stage keeps the same rule (taps from twice its block on: a
 * whole block of time to compute), so the far stage has 371 ms per job; it moves most of a 20 s IR's work into big,
 * cheap FFTs (about 1/8 the memory traffic of 2048-sample partitions).
 * Tail modes:
 *   Threaded  a worker thread computes it (on the Force's other cores); the audio thread only copies samples and
 *             adds the result. If a result isn't ready in time (a "miss"), that block's tail is left out (counted).
 *   Inline    the audio thread computes it when a W-block completes: the same arithmetic, but in one host block of
 *             every W/128 -- a spike (kept for the benchmark, to show why the thread is there).
 *   Uniform   no split: the whole IR in 128-sample partitions on the audio thread (the cab-IR method; reference).
 * The result equals direct convolution with the IR (tools/bench_reverb --check).
 */
#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <semaphore.h>

namespace irrev {

// uniformly partitioned overlap-save convolution of `taps` with block size b; input delayed by `delay` blocks
class Upols {
 public:
  Upols(const float* taps, size_t n, int b, int delay);
  ~Upols();
  Upols(const Upols&) = delete;
  Upols& operator=(const Upols&) = delete;
  // one input block (b samples) in; adds this block's output (b samples) into out
  void push(const float* in, float* out);
  int block() const { return b_; }
  size_t partitions() const { return parts_; }

 private:
  int b_, delay_;
  size_t parts_ = 0, fdl_len_ = 0, pos_ = 0;
  void* setup_ = nullptr;
  float* spec_ = nullptr;      // parts_ * 2b: the IR partitions, transformed
  float* fdl_ = nullptr;       // fdl_len_ * 2b: the transformed input windows (delay + parts)
  float* win_ = nullptr;       // 2b: [previous block, this block]
  float* acc_ = nullptr;       // 2b
  float* tmp_ = nullptr;       // 2b
  float* work_ = nullptr;      // 2b
};

class LongConvolver {
 public:
  enum Mode { Threaded, Inline, Uniform };
  static constexpr int kHost = 128;        // host block (MPC)
  static constexpr int kTail = 2048;       // W

  // far: the far stage's block (0: none; it is used only when the IR reaches past 2 x far). Default: platform.h.
  LongConvolver(const std::vector<float>& ir, Mode mode, int far = -1);
  ~LongConvolver();
  // n must be a multiple of 128 (MPC's block)
  void process(const float* in, float* out, int n);
  long misses() const;
  double worker_cpu_seconds() const;
  size_t length() const { return length_; }
  size_t stages() const { return stages_.size(); }   // tail stages (0, 1 or 2)

 private:
  struct Stage {                           // taps [2W, end) in W-sample blocks, computed one block ahead
    int w = 0;
    std::unique_ptr<Upols> conv;
    std::vector<float> in_ring;            // 4W of input
    std::vector<float> out[3];             // output, W-blocks m % 3
    std::atomic<long> ready{-1};           // last W-block whose output is ready
    std::atomic<long> requested{-1};       // last W-block whose output was asked for
    std::atomic<long long> cpu{0};         // the worker's CPU time, microseconds
    long misses = 0;                       // (audio thread)
    sem_t sem;
    std::thread worker;
  };
  void job(Stage& s, long j);              // input W-block j -> stage output block j + 2
  void worker_main(Stage* s);

  Mode mode_;
  size_t length_;
  std::unique_ptr<Upols> head_;
  std::vector<std::unique_ptr<Stage>> stages_;
  long sample_ = 0;                        // samples processed (audio thread)
  std::atomic<bool> stop_{false};
};

}  // namespace irrev
