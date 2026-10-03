// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
#include "wav_reader.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace irrev {

static uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

std::vector<float> WavData::mono_mix() const {
  if (channels.empty()) return {};
  if (channels.size() == 1) return channels[0];
  const size_t n = channels[0].size();
  std::vector<float> m(n, 0.0f);
  const float k = 1.0f / (float)channels.size();
  for (const auto& ch : channels)
    for (size_t i = 0; i < n && i < ch.size(); i++) m[i] += ch[i];
  for (float& v : m) v *= k;
  return m;
}

bool read_wav(const std::string& path, WavData* out, std::string* err) {
  auto fail = [err](const char* why) { if (err) *err = why; return false; };
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return fail("Can't open the file");
  std::vector<uint8_t> d;
  {
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
      d.insert(d.end(), buf, buf + n);
      if (d.size() > (256u << 20)) { fclose(f); return fail("File too large"); }
    }
    fclose(f);
  }
  if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) != 0 || memcmp(d.data() + 8, "WAVE", 4) != 0)
    return fail("Not a WAV file");

  // walk the chunks: fmt, then data (anything else is skipped; chunks are word-aligned)
  int fmt_tag = -1, nch = 0, bits = 0, block = 0;
  uint32_t rate = 0;
  const uint8_t* data = nullptr;
  size_t data_len = 0;
  for (size_t p = 12; p + 8 <= d.size();) {
    const uint8_t* c = d.data() + p;
    size_t len = u32(c + 4);
    if (p + 8 + len > d.size()) len = d.size() - p - 8;          // a truncated last chunk: use what's there
    if (!memcmp(c, "fmt ", 4) && len >= 16) {
      fmt_tag = u16(c + 8);
      nch = u16(c + 10);
      rate = u32(c + 12);
      block = u16(c + 20);
      bits = u16(c + 22);
      if (fmt_tag == 0xFFFE && len >= 40) fmt_tag = u16(c + 8 + 24);  // EXTENSIBLE: the sub-format GUID's first word
    } else if (!memcmp(c, "data", 4)) {
      data = c + 8;
      data_len = len;
      if (fmt_tag >= 0) break;                                    // fmt seen: done (data after fmt, as usual)
    }
    p += 8 + len + (len & 1);
  }
  if (fmt_tag < 0) return fail("No WAV format chunk");
  if (!data) return fail("No audio in the file");
  if (nch < 1 || rate == 0) return fail("Invalid WAV header");
  const bool pcm = fmt_tag == 1, flt = fmt_tag == 3;
  if (!pcm && !flt) return fail("Unsupported WAV format");
  if ((pcm && bits != 16 && bits != 24 && bits != 32) || (flt && bits != 32 && bits != 64))
    return fail("Unsupported bit depth");
  const int bps = bits / 8;
  if (block < bps * nch) block = bps * nch;
  const size_t frames = data_len / (size_t)block;
  out->rate = rate;
  out->channels.assign(nch, std::vector<float>(frames));
  // the same conversions as AudioDSPTools' wav.cpp (bit-identical), except 32-bit PCM's sign (see wav_reader.h)
  const float s16 = 1.0 / ((double)(1 << 15));
  const float s24 = 1.0 / ((double)(1 << 23));
  const float s32 = 1.0 / 2147483648.0;
  for (size_t i = 0; i < frames; i++) {
    const uint8_t* fr = data + i * block;
    for (int ch = 0; ch < nch; ch++) {
      const uint8_t* s = fr + ch * bps;
      float v;
      if (pcm && bits == 16) v = s16 * (float)(int16_t)u16(s);
      else if (pcm && bits == 24) {
        int x = s[0] | (s[1] << 8) | (s[2] << 16);
        if (x & (1 << 23)) x |= ~((1 << 24) - 1);
        v = s24 * (float)x;
      } else if (pcm) v = s32 * (float)(int32_t)u32(s);
      else if (bits == 32) { uint32_t w = u32(s); memcpy(&v, &w, 4); }
      else { uint64_t w = (uint64_t)u32(s) | ((uint64_t)u32(s + 4) << 32); double dv; memcpy(&dv, &w, 8); v = (float)dv; }
      out->channels[ch][i] = v;
    }
  }
  return true;
}

}  // namespace irrev
