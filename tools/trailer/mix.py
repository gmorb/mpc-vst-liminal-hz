#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""mix.py dark|cyber|trap -- a score's stems (as Liminal Hz played them, render.sh) mixed and mastered: vst/build/trailer/<style>/mix.wav
(44.1 kHz, 16-bit stereo). Each stem is set to a level against the drums (LEVEL, dB, by its loudness while it plays),
then the sum is high-passed, driven gently and peak-limited to -1 dBFS (a 5 ms look-ahead, 120 ms release)."""
import os, sys, wave
import numpy as np
from scipy.signal import butter, sosfilt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if len(sys.argv) > 1: os.environ["TRAILER_STYLE"] = sys.argv[1]       # (the style sets the score's tempo and meter)
import score

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STYLE = sys.argv[1] if len(sys.argv) > 1 else "dark"
T = os.path.join(ROOT, "vst", "build", "trailer", STYLE)
SR = score.SR
LEVEL = score.LEVEL[STYLE]

def read(p):
    with open(p, "rb") as f: d = f.read()
    pos = 12
    while pos + 8 <= len(d):
        tag, sz = d[pos:pos + 4], int.from_bytes(d[pos + 4:pos + 8], "little")
        if tag == b"data": return np.frombuffer(d[pos + 8:pos + 8 + sz], dtype="<f4").reshape(-1, 2).astype(np.float64)
        pos += 8 + sz + (sz & 1)

def active_rms(x):
    m = np.max(np.abs(x), axis=1); hop = 2048
    blocks = [np.sqrt(np.mean(x[i:i + hop] ** 2)) for i in range(0, len(x), hop)]
    b = np.array(blocks); b = b[b > b.max() * 10 ** (-30 / 20)]
    return float(np.sqrt(np.mean(b ** 2)))

def limiter(x, ceiling=10 ** (-1 / 20), look=int(0.005 * SR), rel=0.12):
    peak = np.max(np.abs(x), axis=1)
    # the gain needed at each sample, held over the look-ahead, then released smoothly
    need = np.minimum(1.0, ceiling / np.maximum(peak, 1e-9))
    from scipy.ndimage import minimum_filter1d
    need = minimum_filter1d(need, size=2 * look + 1, origin=0)
    g = np.empty_like(need); a = np.exp(-1 / (rel * SR)); cur = 1.0
    for i, v in enumerate(need):
        cur = v if v < cur else v + (cur - v) * a
        g[i] = cur
    y = x * np.roll(g, -look)[:, None] if False else x * g[:, None]
    return np.clip(y, -ceiling, ceiling)

def main():
    stems = {}
    for k in score.STEMS[STYLE]:
        p = os.path.join(T, k + ("_wet.wav" if score.STEMS[STYLE][k] else ".wav"))
        stems[k] = read(p)
    for k, bar in score.GATES[STYLE]:                                  # tails cut where they'd ring over a hit
        a = int(score.t_of(bar) * SR); f = int(0.015 * SR); b = int(score.t_of(bar + 2) * SR)
        x = stems[k]; x[a - f:a] *= np.linspace(1, 0, f)[:, None]; x[a:b] = 0
    n = min(len(v) for v in stems.values())
    ref = active_rms(stems["drums"][:n])
    mix = np.zeros((n, 2))
    for k, x in stems.items():
        g = ref / max(1e-9, active_rms(x[:n])) * 10 ** (LEVEL[k] / 20)
        mix += x[:n] * g
        print("%-8s %+5.1f dB" % (k, 20 * np.log10(g)))
    mix = sosfilt(butter(2, 28, "high", fs=SR, output="sos"), mix, axis=0)
    mix *= 0.5 / np.max(np.abs(mix))
    mix = np.tanh(mix * 1.8) / np.tanh(1.8) * 0.9                                 # a little glue and weight
    mix = limiter(mix * 2.3)                                                      # (loud, as trailers are)
    fade = int(1.5 * SR); mix[-fade:] *= np.linspace(1, 0, fade)[:, None]
    out = os.path.join(T, "mix.wav")
    with wave.open(out, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(mix, -1, 1) * 32767).astype("<i2").tobytes())
    r = 20 * np.log10(np.sqrt(np.mean(mix ** 2)))
    print("wrote %s: %.1f s, RMS %.1f dBFS, peak %.1f dBFS" % (out, n / SR, r, 20 * np.log10(np.max(np.abs(mix)))))

if __name__ == "__main__":
    main()
