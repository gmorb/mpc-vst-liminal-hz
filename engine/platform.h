// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* platform.h -- what each MPC generation can afford (mpc-vst-liminal-hz).
 *
 * One plugin, two builds (mpc-vst-plugins docs/GEN2.md): the same uid, parameters and presets, so a project moves
 * between generations. Only how much work the engine does differs:
 *
 *                              Gen 1 (armv7, RK3288: 4x A17)        Gen 2 (aarch64, RK3588: 4x A76 + 4x A55)
 *   IR length used             5 s                                  20 s (Decay 300% of a 5 s room fits whole)
 *   convolution stages         head 128 | tail 2048                 head 128 | 2048 | far 16384 (its own worker)
 *   IR resampling (prep)       cubic (AudioDSPTools)                windowed sinc (Kaiser, 64 zero crossings,
 *                                                                   band-limited when Stretch shortens the IR)
 *   Decay analysis (prep)      STFT 2048, hop 512                   STFT 4096, hop 512 (10.8 Hz bins: the bass
 *                                                                   third-octaves get real resolution)
 *   FFT                        PFFFT scalar                         PFFFT NEON (PFFFT_ENABLE_NEON, build.sh)
 *
 * LIMINAL_GEN2 is set by vst/build.sh gen2 (and the gen2 host build, for tests). Nothing here changes a parameter,
 * a preset or the page: a Gen 2 project opened on a Gen 1 device plays its IR cut to 5 s, as Gen 1 always did.
 */
#pragma once

namespace irrev {
namespace platform {
#if defined(LIMINAL_GEN2)
constexpr bool kGen2 = true;
constexpr double kMaxSeconds = 20.0;
constexpr int kFarBlock = 16384;          // the far stage's block (0: none). Taps from 2 x this on.
constexpr bool kSincResample = true;
constexpr int kDecayFFT = 4096;          // (~56 MB of spectra, briefly, for a 20 s stereo IR at Decay 300%)
#else
constexpr bool kGen2 = false;
constexpr double kMaxSeconds = 5.0;
constexpr int kFarBlock = 0;
constexpr bool kSincResample = false;
constexpr int kDecayFFT = 2048;
#endif
constexpr int kDecayHop = 512;            // the same on both: Decay's time resolution (11.6 ms) doesn't change
}  // namespace platform
}  // namespace irrev
