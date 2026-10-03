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

  LongConvolver(const std::vector<float>& ir, Mode mode);
  ~LongConvolver();
  // n must be a multiple of 128 (MPC's block)
  void process(const float* in, float* out, int n);
  long misses() const { return misses_; }
  double worker_cpu_seconds() const { return worker_cpu_.load() / 1e6; }
  size_t length() const { return length_; }

 private:
  void tail_job(long m);                   // the tail for W-block m into tail_out_[m % 3]
  void worker_main();

  Mode mode_;
  size_t length_;
  std::unique_ptr<Upols> head_, tail_;
  std::vector<float> in_ring_;             // 4W of input
  std::vector<float> tail_out_[3];         // tail output, W-blocks m % 3
  long sample_ = 0;                        // samples processed (audio thread)
  std::atomic<long> ready_{-1};            // last W-block whose tail is ready
  std::atomic<long> requested_{-1};        // last W-block whose tail was asked for
  std::atomic<bool> stop_{false};
  std::atomic<long long> worker_cpu_{0};   // microseconds
  long misses_ = 0;
  sem_t sem_;
  std::thread worker_;
};

}  // namespace irrev
