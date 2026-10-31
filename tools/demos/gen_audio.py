#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""gen_audio.py <out dir> -- the dry phrases for the three demos, synthesized (no samples): 44.1 kHz stereo float WAVs.
  pad.wav     two soft, detuned-saw pad chords (A minor add9, F major 7), for the Backrooms demo
  drums.wav   two bars of a synthesized kit at 92 bpm, for the Empty Mall demo
  hits.wav    two bars of sparse kick and snare at 80 bpm, for the reverse-swell demo
Each file is the phrase followed by silence, so the reverb's tail has room to play out."""
import os, sys, wave
import numpy as np

SR = 44100
rng = np.random.default_rng(7)

def write(path, x):
    x = np.asarray(x, dtype=np.float32)
    if x.ndim == 1: x = np.stack([x, x], 1)
    with open(path, "wb") as f:
        n = len(x); data = x.astype("<f4").tobytes()
        f.write(b"RIFF" + (36 + len(data)).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little"))
        f.write((3).to_bytes(2, "little") + (2).to_bytes(2, "little") + SR.to_bytes(4, "little") + (SR * 8).to_bytes(4, "little"))
        f.write((8).to_bytes(2, "little") + (32).to_bytes(2, "little") + b"data" + len(data).to_bytes(4, "little") + data)

def onepole_lp(x, fc):
    a = np.exp(-2 * np.pi * fc / SR); y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x): s = (1 - a) * v + a * s; y[i] = s
    return y

def lp(x, fc, order=2):
    for _ in range(order): x = onepole_lp(x, fc)
    return x

def hp(x, fc): return x - lp(x, fc, 1)

def env_adsr(n, a, d, s, r_start, r):
    t = np.arange(n) / SR; e = np.zeros(n)
    e = np.where(t < a, t / a, np.where(t < a + d, 1 - (1 - s) * (t - a) / d, s))
    rel = t >= r_start; e[rel] = s * np.exp(-(t[rel] - r_start) / r)
    return e

def saw(f, n, phase=0.0):
    t = np.arange(n) / SR; return 2 * ((t * f + phase) % 1.0) - 1

def note_hz(m): return 440.0 * 2 ** ((m - 69) / 12)

def pad(total_s):
    out = np.zeros((int(total_s * SR), 2))
    chords = [([57, 60, 64, 71], 0.3), ([53, 57, 60, 64], 2.7)]      # Am(add9) voicing, then Fmaj7
    hold = 2.0
    for notes, t0 in chords:
        n = int((hold + 1.2) * SR); e = env_adsr(n, 0.12, 0.6, 0.75, hold, 0.35)
        for k, m in enumerate(notes):
            f = note_hz(m)
            for side, det in ((0, -0.07), (1, 0.07)):
                v = (saw(f * 2 ** (det / 12), n, rng.random()) + 0.6 * saw(f * 2 ** (-det / 24), n, rng.random())) * e * 0.06
                v = lp(v, 1400 + 300 * k, 2)
                i0 = int(t0 * SR); out[i0:i0 + n, side] += v[: len(out) - i0]
    return out

def kick(n=int(0.5 * SR)):
    t = np.arange(n) / SR; f = 45 + 95 * np.exp(-t * 28)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7) * 0.9 + 0.15 * np.exp(-t * 300) * rng.standard_normal(n)

def snare(n=int(0.35 * SR)):
    t = np.arange(n) / SR
    body = np.sin(2 * np.pi * 185 * t) * np.exp(-t * 22) * 0.45
    noise = hp(lp(rng.standard_normal(n), 7000, 1), 900) * np.exp(-t * 16) * 0.55
    return body + noise

def hat(n=int(0.09 * SR), level=0.18):
    t = np.arange(n) / SR; return hp(rng.standard_normal(n), 7000) * np.exp(-t * 60) * level

def place(out, x, t, pan=0.0):
    i = int(t * SR); x = x[: len(out) - i]
    out[i:i + len(x), 0] += x * (1 - max(0, pan)); out[i:i + len(x), 1] += x * (1 + min(0, pan))

def drums(total_s, bpm=92):
    out = np.zeros((int(total_s * SR), 2)); beat = 60 / bpm
    for bar in range(2):
        b0 = 0.15 + bar * 4 * beat
        for k in (0, 1.5, 2): place(out, kick(), b0 + k * beat)
        for k in (1, 3): place(out, snare(), b0 + k * beat)
        for k in range(8): place(out, hat(level=0.16 if k % 2 else 0.11), b0 + k * beat / 2, pan=0.25)
    return out

def hits(total_s, bpm=80):
    out = np.zeros((int(total_s * SR), 2)); beat = 60 / bpm
    for bar in range(2):
        b0 = 0.15 + bar * 4 * beat
        place(out, kick(), b0); place(out, kick(), b0 + 2.5 * beat)
        for k in (1, 3): place(out, snare() * 1.1, b0 + k * beat)
    return out

def norm(x, peak=0.7):
    m = np.max(np.abs(x)); return x * (peak / m) if m > 0 else x

if __name__ == "__main__":
    d = sys.argv[1]; os.makedirs(d, exist_ok=True)
    write(os.path.join(d, "pad.wav"), norm(pad(13.0)))        # 5 s of chords, then room for the tail
    write(os.path.join(d, "drums.wav"), norm(drums(9.5)))     # 2 bars (5.2 s), then the tail
    write(os.path.join(d, "hits.wav"), norm(hits(9.5)))       # 2 bars (6.0 s), then the tail
    print("wrote", d)
