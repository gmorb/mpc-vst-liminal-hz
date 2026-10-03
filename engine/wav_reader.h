// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
/* wav_reader.h -- WAV files for impulse responses (mpc-vst-nam-a2).
 *
 * Reads what IR libraries actually ship, which AudioDSPTools' loader (the official NAM plugin's) partly refuses:
 *   - PCM 16/24/32-bit and IEEE float 32/64-bit;
 *   - WAVE_FORMAT_EXTENSIBLE (what many DAWs write), with or without a `fact` chunk (the WAV spec asks for `fact`
 *     only for compressed formats; AudioDSPTools requires it and calls such files "invalid");
 *   - any number of channels, returned separately (mono_mix() averages them);
 *   - any other chunks (JUNK, bext, LIST, cue, ...) skipped.
 * Sample values are converted exactly as AudioDSPTools does (bit-identical for files it accepts: tools/wav_test),
 * except 32-bit integer PCM: AudioDSPTools scales it by 1 / (double)(1 << 31), and `1 << 31` overflows to a negative
 * int, so it loads those files with inverted polarity; here the scale is +1/2^31.
 */
#pragma once
#include <string>
#include <vector>

namespace irrev {

struct WavData {
  double rate = 0;
  std::vector<std::vector<float>> channels;    // channels[c][frame]
  std::vector<float> mono_mix() const;         // the channels averaged (the one channel, for mono files)
};

// false with a short reason in *err (for the plugin's info line: "Not a WAV file", "Unsupported WAV format", ...)
bool read_wav(const std::string& path, WavData* out, std::string* err);

}  // namespace irrev
