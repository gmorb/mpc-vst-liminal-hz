// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "long_convolver.h"
#include <time.h>
#include <algorithm>
#include <cstring>
#include "pffft.h"

namespace irrev {

static float* falloc(size_t n) {
  float* p = (float*)pffft_aligned_malloc(n * sizeof(float));
  memset(p, 0, n * sizeof(float));
  return p;
}

static long long cpu_us() {
  timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return (long long)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

// ------------------------------------------------------------------------------------------------ Upols
Upols::Upols(const float* taps, size_t n, int b, int delay) : b_(b), delay_(delay) {
  const int N = 2 * b;
  setup_ = pffft_new_setup(N, PFFFT_REAL);
  parts_ = (n + b - 1) / b;
  if (parts_ == 0) parts_ = 1;
  fdl_len_ = parts_ + delay_;
  spec_ = falloc(parts_ * N);
  fdl_ = falloc(fdl_len_ * N);
  win_ = falloc(N);
  acc_ = falloc(N);
  tmp_ = falloc(N);
  work_ = falloc(N);
  for (size_t k = 0; k < parts_; k++) {        // each partition: [h_k, 0...0], transformed (PFFFT's internal order)
    memset(tmp_, 0, N * sizeof(float));
    const size_t first = k * b, count = std::min((size_t)b, n - first);
    if (first < n) memcpy(tmp_, taps + first, count * sizeof(float));
    pffft_transform((PFFFT_Setup*)setup_, tmp_, spec_ + k * N, work_, PFFFT_FORWARD);
  }
}

Upols::~Upols() {
  if (setup_) pffft_destroy_setup((PFFFT_Setup*)setup_);
  for (float* p : {spec_, fdl_, win_, acc_, tmp_, work_}) if (p) pffft_aligned_free(p);
}

void Upols::push(const float* in, float* out) {
  const int N = 2 * b_;
  memmove(win_, win_ + b_, b_ * sizeof(float));            // overlap-save: [previous block, this block]
  memcpy(win_ + b_, in, b_ * sizeof(float));
  pffft_transform((PFFFT_Setup*)setup_, win_, fdl_ + pos_ * N, work_, PFFFT_FORWARD);
  memset(acc_, 0, N * sizeof(float));
  const float scale = 1.0f / N;
  for (size_t k = 0; k < parts_; k++) {
    const size_t idx = (pos_ + fdl_len_ - ((size_t)delay_ + k) % fdl_len_) % fdl_len_;
    pffft_zconvolve_accumulate((PFFFT_Setup*)setup_, fdl_ + idx * N, spec_ + k * N, acc_, scale);
  }
  pffft_transform((PFFFT_Setup*)setup_, acc_, tmp_, work_, PFFFT_BACKWARD);
  for (int i = 0; i < b_; i++) out[i] += tmp_[b_ + i];       // the valid half
  pos_ = (pos_ + 1) % fdl_len_;
}

// ------------------------------------------------------------------------------------------------ LongConvolver
LongConvolver::LongConvolver(const std::vector<float>& ir, Mode mode) : mode_(mode), length_(ir.size()) {
  sem_init(&sem_, 0, 0);
  const size_t n = ir.empty() ? 1 : ir.size();
  std::vector<float> h = ir.empty() ? std::vector<float>(1, 0.0f) : ir;
  if (mode_ == Uniform || n <= (size_t)2 * kTail) {
    head_.reset(new Upols(h.data(), n, kHost, 0));
    return;
  }
  head_.reset(new Upols(h.data(), 2 * kTail, kHost, 0));
  tail_.reset(new Upols(h.data() + 2 * kTail, n - 2 * kTail, kTail, 0));
  in_ring_.assign(4 * kTail, 0.0f);
  for (auto& t : tail_out_) t.assign(kTail, 0.0f);
  ready_ = 1;                                               // output blocks 0 and 1 have no tail (it starts at 2W)
  if (mode_ == Threaded) worker_ = std::thread(&LongConvolver::worker_main, this);
}

LongConvolver::~LongConvolver() {
  stop_ = true;
  sem_post(&sem_);
  if (worker_.joinable()) worker_.join();
  sem_destroy(&sem_);
}

void LongConvolver::tail_job(long j) {                     // input W-block j -> the tail of output block j + 2
  std::vector<float>& out = tail_out_[(j + 2) % 3];
  std::fill(out.begin(), out.end(), 0.0f);
  tail_->push(&in_ring_[(j % 4) * kTail], out.data());
  ready_.store(j + 2, std::memory_order_release);
}

void LongConvolver::worker_main() {
  long done = -1;
  for (;;) {
    sem_wait(&sem_);
    if (stop_) return;
    const long want = requested_.load(std::memory_order_acquire);
    while (done < want) {                                   // in order; catches up if it fell behind
      const long long t0 = cpu_us();
      tail_job(++done);
      worker_cpu_ += cpu_us() - t0;
    }
  }
}

void LongConvolver::process(const float* in, float* out, int n) {
  for (int off = 0; off < n; off += kHost) {
    float* o = out + off;
    memset(o, 0, kHost * sizeof(float));
    head_->push(in + off, o);
    if (!tail_) { sample_ += kHost; continue; }
    // the tail of this output W-block, if it's there
    const long m = sample_ / kTail;
    const int at = (int)(sample_ % kTail);
    if (ready_.load(std::memory_order_acquire) >= m) {
      const float* t = &tail_out_[m % 3][at];
      for (int i = 0; i < kHost; i++) o[i] += t[i];
    } else if (at == 0) {
      misses_++;                                            // late: this W-block plays without its tail
    }
    memcpy(&in_ring_[(sample_ % (4 * kTail))], in + off, kHost * sizeof(float));
    sample_ += kHost;
    if (sample_ % kTail == 0) {                             // input W-block j complete
      const long j = sample_ / kTail - 1;
      if (mode_ == Inline) {
        tail_job(j);
      } else {
        requested_.store(j, std::memory_order_release);
        sem_post(&sem_);
      }
    }
  }
}

}  // namespace irrev
