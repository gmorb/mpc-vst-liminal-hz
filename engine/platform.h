// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* platform.h -- what each MPC generation does, and the LONG IR quality setting (mpc-vst-liminal-hz).
 *
 * One plugin, two builds (mpc-vst-plugins docs/GEN2.md): the same uid, parameters and presets, so a project moves
 * between generations.
 *
 * Always, on both builds (each makes the engine cheaper, so there is nothing to switch):
 *   - three convolution stages: head 128 (audio thread) | 2048 | far 16384 (each tail stage on its own worker). The far
 *     stage takes everything past 0.74 s in large blocks: a 5 s IR needs about a quarter of the 2048-only partitions;
 *   - PFFFT's NEON code (-DPFFFT_ENABLE_NEON, vst/build.sh; before 1.3.0 the Gen 1 build ran PFFFT scalar).
 *
 * The quality of IR preparation (all off the audio thread), Quality below:
 *                        standard (Gen 1, LONG IR off)       high (Gen 1 with LONG IR on; Gen 2 always)
 *   IR length used       5 s                                 20 s (Decay 300% of a 5 s room fits whole)
 *   IR resampling        cubic (AudioDSPTools)               windowed sinc (Kaiser, 64 zero crossings, band-limited
 *                                                            when Stretch shortens the IR)
 *   Decay analysis       STFT 2048, hop 512                  STFT 4096, hop 512 (10.8 Hz bins: real bass resolution)
 * High costs more memory, a longer preparation and (for long IRs) more worker CPU, so on Gen 1 it is the page's LONG IR
 * button (two taps: the first warns). LIMINAL_GEN2 (vst/build.sh gen2) makes high the only quality.
 */
#pragma once

namespace irrev {

struct Quality {
  double max_seconds = 5.0;   // the IR length used (after Stretch)
  bool sinc = false;          // band-limited sinc resampling (else cubic)
  int decay_fft = 2048;       // Decay's STFT size (hop 512)
  static Quality standard() { return Quality(); }
  static Quality high() { Quality q; q.max_seconds = 20.0; q.sinc = true; q.decay_fft = 4096; return q; }
  bool operator==(const Quality& o) const { return max_seconds == o.max_seconds && sinc == o.sinc && decay_fft == o.decay_fft; }
  bool operator!=(const Quality& o) const { return !(*this == o); }
};

namespace platform {
#if defined(LIMINAL_GEN2)
constexpr bool kGen2 = true;                // high quality always (the LONG IR button reads "always on")
#else
constexpr bool kGen2 = false;
#endif
constexpr int kFarBlock = 16384;            // the far convolution stage's block (taps from 2 x this on), both builds
constexpr int kDecayHop = 512;              // Decay's time resolution (11.6 ms), both qualities
inline Quality default_quality() { return kGen2 ? Quality::high() : Quality::standard(); }
inline Quality quality(bool long_ir) { return (kGen2 || long_ir) ? Quality::high() : Quality::standard(); }
}  // namespace platform
}  // namespace irrev
