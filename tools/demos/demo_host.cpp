// SPDX-License-Identifier: MIT
// Copyright (c) 2026 the mpc-vst-liminal-hz contributors
// demo_host.cpp -- runs the Liminal Hz plugin (.so) over a stereo float WAV the way MPC does (128-frame blocks) and
// records what its page would show, for the demo videos.
//   demo_host <plugin.so> <in.wav> <out.wav> <state.json> <preset 0..14> [key=value ...] [long_ir=1]
// key=value: a parameter by its params.json key, as a real value (mix=55, decay=300); applied after the preset.
// state.json: every parameter's normalised value and display text after loading, and the playhead (0..24) and the
// 12 wave columns at 30 frames a second while the audio plays.
#include <dlfcn.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
struct AEffect;
typedef intptr_t (*hostcb)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
  int32_t magic; intptr_t (*d)(AEffect*, int32_t, int32_t, intptr_t, void*, float); void* p;
  void (*setP)(AEffect*, int32_t, float); float (*getP)(AEffect*, int32_t);
  int32_t numPrograms, numParams, numInputs, numOutputs, flags; intptr_t r1, r2; int32_t a, b, c; float io;
  void *obj, *user; int32_t uid, ver; void (*pr)(AEffect*, float**, float**, int32_t); void* pdr; char f[56];
};
static intptr_t host(AEffect*, int32_t op, int32_t, intptr_t, void*, float) { return op == 1 ? 2400 : 0; }
static std::string disp(AEffect* a, int i) { char b[256] = {0}; a->d(a, 7, i, 0, b, 0); return b; }
static std::string pname(AEffect* a, int i) { char b[64] = {0}; a->d(a, 8, i, 0, b, 0); return b; }
static std::string esc(const std::string& s) {
  std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
}
struct Wav { int ch = 2; int rate = 44100; std::vector<float> l, r; };
static bool read_wav(const char* p, Wav& w) {   // 32-bit float stereo, as gen_audio.py writes
  FILE* f = fopen(p, "rb"); if (!f) return false;
  std::vector<unsigned char> d; unsigned char b[65536]; size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
  fclose(f);
  size_t pos = 12; const float* data = nullptr; size_t bytes = 0;
  while (pos + 8 <= d.size()) {
    uint32_t sz; memcpy(&sz, &d[pos + 4], 4);
    if (!memcmp(&d[pos], "fmt ", 4)) { uint16_t ch; uint32_t rate; memcpy(&ch, &d[pos + 10], 2); memcpy(&rate, &d[pos + 12], 4); w.ch = ch; w.rate = rate; }
    if (!memcmp(&d[pos], "data", 4)) { data = (const float*)&d[pos + 8]; bytes = sz; }
    pos += 8 + sz + (sz & 1);
  }
  if (!data) return false;
  const size_t frames = bytes / 4 / w.ch;
  w.l.resize(frames); w.r.resize(frames);
  for (size_t i = 0; i < frames; i++) { w.l[i] = data[i * w.ch]; w.r[i] = data[i * w.ch + (w.ch > 1 ? 1 : 0)]; }
  return true;
}
static void write_wav(const char* p, const std::vector<float>& l, const std::vector<float>& r, int rate) {
  FILE* f = fopen(p, "wb");
  const uint32_t n = (uint32_t)l.size(), bytes = n * 8, riff = 36 + bytes, fmt = 16, br = rate * 8;
  const uint16_t tag = 3, ch = 2, align = 8, bits = 32;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
  fwrite(&tag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
  for (uint32_t i = 0; i < n; i++) { fwrite(&l[i], 4, 1, f); fwrite(&r[i], 4, 1, f); }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 6) { fprintf(stderr, "usage: demo_host plugin.so in.wav out.wav state.json preset [key=value...]\n"); return 2; }
  void* so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!so) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  auto entry = (AEffect* (*)(hostcb))dlsym(so, "VSTPluginMain");
  Wav in; if (!read_wav(argv[2], in)) { fprintf(stderr, "can't read %s\n", argv[2]); return 1; }
  AEffect* a = entry(host);
  a->d(a, 0, 0, 0, nullptr, 0); a->d(a, 10, 0, 0, nullptr, (float)in.rate); a->d(a, 11, 0, 128, nullptr, 0); a->d(a, 12, 0, 1, nullptr, 0);
  const int np = a->numParams;
  auto idx = [&](const char* name) { for (int i = 0; i < np; i++) if (pname(a, i) == name) return i; return -1; };
  auto idle = [&] { const std::string s = disp(a, idx("IR Info")); return s != "Searching..." && s != "Loading..."; };
  auto wait = [&] { for (int k = 0; k < 3000 && !idle(); k++) std::this_thread::sleep_for(std::chrono::milliseconds(10)); };
  wait();
  const int preset = atoi(argv[5]);
  a->setP(a, idx("Preset"), preset / 14.0f);
  wait();
  // overrides: params.json key=real value; the plugin's own normalisation is found by bisection on its display text
  // being reached is overkill: each key's range is given here (the page shows real values, the plugin takes 0..1)
  struct R { const char* key; const char* name; double lo, hi; bool log; };
  const R ranges[] = {{"mix", "Mix", 0, 100, 0}, {"predelay", "Pre-delay", 0, 250, 0}, {"lowcut", "Low Cut", 20, 1000, 1},
                      {"highcut", "High Cut", 1000, 20000, 1}, {"width", "Width", 0, 200, 0}, {"length", "Length", 5, 100, 0},
                      {"output", "Output", -24, 12, 0}, {"start", "Start", 0, 90, 0}, {"fade_in", "Fade In", 0, 50, 0},
                      {"fade_out", "Fade Out", 0, 50, 0}, {"stretch", "Stretch", 50, 200, 0}, {"feedback", "Feedback", 0, 100, 0},
                      {"decay", "Decay", 50, 300, 0}, {"reverse", "Reverse", 0, 1, 0}, {"long_ir", "Long IR", 0, 1, 0}};
  for (int k = 6; k < argc; k++) {
    std::string kv = argv[k]; size_t eq = kv.find('='); if (eq == std::string::npos) continue;
    std::string key = kv.substr(0, eq); double v = atof(kv.c_str() + eq + 1);
    for (const R& r : ranges) if (key == r.key) {
      const double n = r.log ? std::log(v / r.lo) / std::log(r.hi / r.lo) : (v - r.lo) / (r.hi - r.lo);
      a->setP(a, idx(r.name), (float)n);
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  wait();
  // play: 128-frame blocks; one state sample every 1/30 s of audio
  const size_t n = in.l.size();
  std::vector<float> ol(n + 128), orr(n + 128), il(128), ir(128), bl(128), br(128);
  std::string frames = "";
  const int iph = idx("Playhead");
  // the display is read on its own thread, as MPC's UI thread does (the audio thread never waits on the plugin's lock)
  std::atomic<bool> late{false}, done{false};
  const auto t_start = std::chrono::steady_clock::now();
  const int iinfo = idx("IR Info");
  std::thread ui([&] { while (!done) { if (disp(a, iinfo).rfind("Tail late", 0) == 0 && !late) { late = true;
                                         fprintf(stderr, "first late block seen at %.2f s of audio\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count()); }
                                       std::this_thread::sleep_for(std::chrono::milliseconds(100)); } });
  double next = 0;
  for (size_t p = 0; p < n; p += 128) {
    // MPC's clock: one 128-frame block every 2.9 ms (the tail's worker threads need real time, as on the device)
    std::this_thread::sleep_until(t_start + std::chrono::microseconds((long long)(1e6 * (double)p / in.rate)));
    for (int i = 0; i < 128; i++) { il[i] = p + i < n ? in.l[p + i] : 0; ir[i] = p + i < n ? in.r[p + i] : 0; }
    float* ins[2] = {il.data(), ir.data()}; float* outs[2] = {bl.data(), br.data()};
    a->pr(a, ins, outs, 128);
    for (int i = 0; i < 128 && p + i < n; i++) { ol[p + i] = bl[i]; orr[p + i] = br[i]; }
    while ((double)(p + 128) / in.rate >= next) {
      const int ph = (int)lround(a->getP(a, iph) * 24);
      frames += (frames.empty() ? "" : ",") + std::to_string(ph);
      next += 1.0 / 30;
    }
  }
  done = true; ui.join();
  ol.resize(n); orr.resize(n);
  write_wav(argv[3], ol, orr, in.rate);
  FILE* js = fopen(argv[4], "w");
  fprintf(js, "{\n \"params\": [\n");
  for (int i = 0; i < np; i++)
    fprintf(js, "  {\"i\": %d, \"name\": \"%s\", \"norm\": %.6f, \"text\": \"%s\"}%s\n", i, esc(pname(a, i)).c_str(), a->getP(a, i),
            esc(disp(a, i)).c_str(), i + 1 < np ? "," : "");
  fprintf(js, " ],\n \"playhead\": [%s]\n}\n", frames.c_str());
  fclose(js);
  printf("ok: %zu frames, IR [%s], info [%s], sonics [%s]\n", n, disp(a, idx("IR")).c_str(), disp(a, idx("IR Info")).c_str(),
         disp(a, idx("Sonics")).c_str());
  a->d(a, 1, 0, 0, nullptr, 0);
  if (late) { fprintf(stderr, "late tail blocks during the render: run it again\n"); return 3; }
  return 0;
}
