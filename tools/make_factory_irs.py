#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_factory_irs.py [out dir] -- Liminal Hz's factory IRs: synthetic, atmospheric spaces, echoes and strange
rooms, generated from scratch (no recordings: free to ship under the plugin's licence). Default out dir:
packaging/factory/<Category>/<Name_With_Underscores>.wav (the page shows the names with spaces). Stereo, 24-bit, 44.1 kHz, deterministic (fixed seeds).

Each IR = early reflections (softened taps, panned) + a diffuse tail: independent noise per channel, split into
frequency bands that each decay at their own rate (T60 per band: what makes a space dark, bright, carpeted, tiled),
plus a few deliberate oddities (hum, beating tones, swells, brightening) for the "liminal" character.
Checked by --check: no clipping, no DC, a clean start, a decaying (or, for Reverse Bloom, swelling) envelope, and
stereo decorrelation."""
import os, sys, struct
import numpy as np

SR = 44100
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANCHORS = np.array([125.0, 500.0, 2000.0, 8000.0])                     # where each IR's 4 decay times are set
SUB = np.geomspace(30, 18000, 16)                                       # 16 smooth, overlapping sub-bands


def sub_band_weights(f):
    """complementary raised-cosine weights on a log-frequency axis: they sum to 1 at every frequency (no steps)"""
    lf = np.log(np.maximum(f, 1.0)); centers = np.log(SUB)
    w = np.zeros((len(SUB), len(f)))
    for k, c in enumerate(centers):
        lo = centers[k - 1] if k > 0 else c - (centers[1] - centers[0])
        hi = centers[k + 1] if k + 1 < len(centers) else c + (centers[-1] - centers[-2])
        up = (lf >= lo) & (lf <= c); dn = (lf > c) & (lf <= hi)
        w[k, up] = 0.5 * (1 - np.cos(np.pi * (lf[up] - lo) / (c - lo)))
        w[k, dn] = 0.5 * (1 + np.cos(np.pi * (lf[dn] - c) / (hi - c)))
        if k == 0: w[k, lf < c] = 1.0
        if k == len(SUB) - 1: w[k, lf > c] = 1.0
    return w / np.maximum(w.sum(axis=0), 1e-9)


def anchor_mix(fc):
    """how much each of the 4 anchors applies at frequency fc (linear on log f, clamped): weights summing to 1"""
    x = np.interp(np.log(fc), np.log(ANCHORS), np.arange(4))
    m = np.zeros(4); i = int(np.floor(x)); t = x - i
    if i >= 3: m[3] = 1
    else: m[i] = 1 - t; m[i + 1] = t
    return m


def tail(n, t60s, rng, onset=0.0, build=0.02, gains=(1, 1, 1, 1), envs=None):
    """a diffuse tail whose decay changes smoothly with frequency: the 4 given T60s (at 125 Hz, 500 Hz, 2 kHz,
    8 kHz) interpolated over 16 overlapping sub-bands of independent noise; starts at onset, short build-up"""
    t = np.arange(n) / SR
    X = np.fft.rfft(rng.standard_normal(n)); f = np.fft.rfftfreq(n, 1 / SR)
    W = sub_band_weights(f)
    tt = np.maximum(t - onset, 0)
    build_env = (1 - np.exp(-tt / max(build, 1e-4))) * (t >= onset)
    y = np.zeros(n)
    for k, fc in enumerate(SUB):
        m = anchor_mix(fc)
        t60 = float(np.exp(np.dot(m, np.log(np.asarray(t60s, float)))))
        g = float(np.dot(m, np.asarray(gains, float)))
        env = np.exp(-6.91 * tt / t60) * build_env
        if envs is not None: env = env * sum(m[a] * envs[a](t) for a in range(4) if m[a] > 0)
        y += g * np.fft.irfft(X * W[k], n) * env
    return y


def taps(n, delays_ms, gains, soft=12):
    """early reflections: short softened clicks (a Hann pulse of `soft` samples) at given delays"""
    y = np.zeros(n)
    pulse = np.hanning(soft); pulse /= pulse.sum()
    for d, g in zip(delays_ms, gains):
        i = int(d * SR / 1000)
        if i + soft < n: y[i:i + soft] += g * pulse * 3
    return y


def resonance(n, freqs, t60, gain, rng, detune=0.0):
    """decaying sine resonances (modes): a tonal ring the input excites"""
    t = np.arange(n) / SR
    y = np.zeros(n)
    for f in freqs:
        ph = rng.uniform(0, 2 * np.pi)
        y += np.sin(2 * np.pi * (f + rng.uniform(-detune, detune)) * t + ph) * np.exp(-6.91 * t / t60)
    return gain * y / max(1, len(freqs))


def lowpass(x, fc):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
    X *= 1 / (1 + (f / fc) ** 4)
    return np.fft.irfft(X, len(x))


def echoes(n, period_ms, count, decay, rng, smear_t60=0.25, lp_start=9000, lp_step=0.72, pingpong=False):
    """repeats every period_ms, each a little darker (lp) and smeared into a short diffuse burst; returns L, R"""
    L, R = np.zeros(n), np.zeros(n)
    burst_n = int(0.6 * SR)
    for k in range(1, count + 1):
        i = int(k * period_ms * SR / 1000)
        if i >= n: break
        g = decay ** (k - 1)
        b = tail(burst_n, (smear_t60, smear_t60, smear_t60 * 0.7, smear_t60 * 0.5), rng, build=0.004)
        b = lowpass(b, lp_start * lp_step ** (k - 1))
        m = min(burst_n, n - i)
        side = (k % 2) if pingpong else None
        gl = g * (1.0 if side in (None, 1) else 0.25)
        gr = g * (1.0 if side in (None, 0) else 0.25)
        L[i:i + m] += gl * b[:m]
        R[i:i + m] += gr * (b[:m] if pingpong else tail(burst_n, (smear_t60,) * 4, rng, build=0.004)[:m])
    return L, R


# ---- the factory: name -> (category, seconds, builder(n, rng) -> (L, R)) --------------------------------------------
def empty_mall(n, rng):
    er_d = sorted(rng.uniform(18, 140, 22)); er_g = 0.55 * np.exp(-np.array(er_d) / 90)
    L = taps(n, er_d, er_g * rng.uniform(0.5, 1, 22)) + tail(n, (3.6, 3.2, 2.4, 1.6), rng, onset=0.03, build=0.06)
    R = taps(n, er_d[::-1], er_g * rng.uniform(0.5, 1, 22)) + tail(n, (3.6, 3.2, 2.4, 1.6), rng, onset=0.03, build=0.06)
    return L, R

def pool_rooms(n, rng):
    fl = [9.1 * k for k in range(1, 40)]; g = [0.5 * 0.93 ** k for k in range(39)]     # tile flutter
    L = taps(n, fl, g, soft=6) + tail(n, (2.6, 2.8, 2.9, 2.4), rng, onset=0.012, build=0.03, gains=(0.8, 1, 1.1, 1.05))
    R = taps(n, [d + 4.3 for d in fl], g, soft=6) + tail(n, (2.6, 2.8, 2.9, 2.4), rng, onset=0.012, build=0.03, gains=(0.8, 1, 1.1, 1.05))
    return L, R

def fluorescent_hall(n, rng):
    hum = resonance(n, [120, 240, 360], 3.4, 0.05, rng)                              # the lights' 120 Hz hum
    L = tail(n, (4.2, 3.8, 2.6, 1.7), rng, onset=0.04, build=0.08) + hum
    R = tail(n, (4.2, 3.8, 2.6, 1.7), rng, onset=0.045, build=0.08) + hum * 0.9
    return L, R

def backrooms(n, rng):
    hum = resonance(n, [60, 85, 170], 1.6, 0.06, rng, detune=0.6)
    L = taps(n, [11, 23, 37], [0.3, 0.2, 0.14]) + tail(n, (1.6, 1.3, 0.7, 0.35), rng, onset=0.008, build=0.02, gains=(1.1, 1, 0.6, 0.3)) + hum
    R = taps(n, [14, 27, 41], [0.3, 0.2, 0.14]) + tail(n, (1.6, 1.3, 0.7, 0.35), rng, onset=0.009, build=0.02, gains=(1.1, 1, 0.6, 0.3)) + hum
    return L, R

def stairwell(n, rng):
    fl = [31 * k for k in range(1, 50)]; g = [0.6 * 0.9 ** k for k in range(49)]
    L = taps(n, fl, g, soft=20) + 0.6 * tail(n, (2.8, 2.6, 1.9, 1.2), rng, onset=0.03, build=0.1)
    R = taps(n, [d + 6 for d in fl], g, soft=20) + 0.6 * tail(n, (2.8, 2.6, 1.9, 1.2), rng, onset=0.03, build=0.1)
    return L, R

def tape_corridor(n, rng):
    L, R = echoes(n, 333, 12, 0.72, rng, smear_t60=0.3, lp_start=7000, lp_step=0.8)
    L += 0.35 * tail(n, (2.4, 2.1, 1.4, 0.8), rng, onset=0.01, build=0.05)
    R += 0.35 * tail(n, (2.4, 2.1, 1.4, 0.8), rng, onset=0.01, build=0.05)
    L[:int(0.02 * SR)] += taps(int(0.02 * SR), [2], [0.4])
    return L, R

def dream_pingpong(n, rng):
    L, R = echoes(n, 281, 14, 0.78, rng, smear_t60=0.45, lp_start=12000, lp_step=0.88, pingpong=True)
    L += 0.25 * tail(n, (3.2, 3.0, 2.6, 2.2), rng, onset=0.02, build=0.2)
    R += 0.25 * tail(n, (3.2, 3.0, 2.6, 2.2), rng, onset=0.02, build=0.2)
    return L, R

def parking_slap(n, rng):
    L = taps(n, [92, 184, 276], [0.7, 0.35, 0.17], soft=24) + 0.8 * tail(n, (3.4, 2.9, 1.8, 1.0), rng, onset=0.02, build=0.07)
    R = taps(n, [97, 194, 291], [0.7, 0.35, 0.17], soft=24) + 0.8 * tail(n, (3.4, 2.9, 1.8, 1.0), rng, onset=0.02, build=0.07)
    return L, R

def intercom_echo(n, rng):
    """a mall PA: telephone-band repeats (300 Hz - 3.4 kHz), slightly smeared, in a big dead space"""
    L, R = echoes(n, 145, 22, 0.8, rng, smear_t60=0.12, lp_start=3400, lp_step=0.96)
    for x in (L, R):
        X = np.fft.rfft(x); f = np.fft.rfftfreq(n, 1 / SR)
        X *= 1 / (1 + (300 / np.maximum(f, 1)) ** 4)                       # band: high-pass at 300 Hz
        x[:] = np.fft.irfft(X, n)
    L += 0.3 * tail(n, (2.6, 2.2, 1.2, 0.6), rng, onset=0.02, build=0.08)
    R += 0.3 * tail(n, (2.6, 2.2, 1.2, 0.6), rng, onset=0.02, build=0.08)
    return L, R

def glass_void(n, rng):
    modes = [523.3, 787.1, 1294.6, 2003.8, 2711.2, 3389.0]                          # inharmonic: crystal, not chord
    L = resonance(n, modes, 4.2, 0.9, rng, detune=1.5) + 0.25 * tail(n, (1.8, 2.2, 3.2, 3.6), rng, onset=0.01, build=0.3)
    R = resonance(n, modes, 4.2, 0.9, rng, detune=1.5) + 0.25 * tail(n, (1.8, 2.2, 3.2, 3.6), rng, onset=0.01, build=0.3)
    return L, R

def reverse_bloom(n, rng):
    t = np.arange(n) / SR; peak = 1.9
    swell = np.where(t < peak, np.exp(4.5 * (t - peak)), np.exp(-(t - peak) / 0.12))   # a swell, then a quick fall
    L = tail(n, (40, 40, 40, 40), rng, build=0.001) * swell
    R = tail(n, (40, 40, 40, 40), rng, build=0.001) * swell
    return L, R

def sub_tunnel(n, rng):
    rum = resonance(n, [41, 55, 68], 4.0, 0.18, rng, detune=0.4)
    L = taps(n, [140, 290], [0.25, 0.12], soft=40) + tail(n, (4.6, 3.6, 1.6, 0.7), rng, onset=0.05, build=0.15, gains=(1.3, 1, 0.5, 0.25)) + rum
    R = taps(n, [151, 305], [0.25, 0.12], soft=40) + tail(n, (4.6, 3.6, 1.6, 0.7), rng, onset=0.05, build=0.15, gains=(1.3, 1, 0.5, 0.25)) + rum
    return L, R

def shimmer_fog(n, rng):
    grow = [lambda t: 1.0, lambda t: 1.0, lambda t: 1 + 1.5 * np.minimum(t / 2.5, 1), lambda t: 1 + 3 * np.minimum(t / 3.0, 1)]
    L = tail(n, (2.4, 3.0, 3.6, 4.0), rng, onset=0.03, build=0.4, gains=(0.7, 0.9, 0.8, 0.6), envs=grow)
    R = tail(n, (2.4, 3.0, 3.6, 4.0), rng, onset=0.03, build=0.4, gains=(0.7, 0.9, 0.8, 0.6), envs=grow)
    return L, R

def liminal_hz(n, rng):
    t = np.arange(n) / SR
    beat = resonance(n, [220.0, 221.3, 329.6, 330.4, 440.0, 441.7], 4.4, 0.35, rng)   # slow beating pairs
    breathe = 1 + 0.35 * np.sin(2 * np.pi * 0.7 * t)                                  # a breathing tremolo
    L = (tail(n, (3.8, 3.4, 2.6, 1.8), rng, onset=0.03, build=0.12) + beat) * breathe
    R = (tail(n, (3.8, 3.4, 2.6, 1.8), rng, onset=0.033, build=0.12) + beat * 0.95) * (1 + 0.35 * np.sin(2 * np.pi * 0.7 * t + 1.1))
    return L, R

FACTORY = [
    ("Spaces", "Empty Mall", 4.2, empty_mall), ("Spaces", "Pool Rooms", 3.6, pool_rooms),
    ("Spaces", "Fluorescent Hall", 4.8, fluorescent_hall), ("Spaces", "Backrooms", 2.2, backrooms),
    ("Echoes", "Stairwell Flutter", 3.4, stairwell), ("Echoes", "Tape Corridor", 4.6, tape_corridor),
    ("Echoes", "Dream Ping-Pong", 4.8, dream_pingpong), ("Echoes", "Parking Slap", 4.0, parking_slap),
    ("Echoes", "Intercom Echo", 3.8, intercom_echo),
    ("Strange", "Glass Void", 4.8, glass_void), ("Strange", "Reverse Bloom", 2.3, reverse_bloom),
    ("Strange", "Sub Tunnel", 4.8, sub_tunnel), ("Strange", "Shimmer Fog", 4.8, shimmer_fog),
    ("Strange", "Liminal Hz", 4.8, liminal_hz),
]


def finish(L, R, reverse=False):
    """DC out, a short fade at the end, peak to -1 dBFS"""
    n = len(L)
    for x in (L, R):
        x -= np.mean(x)
        f = min(n // 20, int(0.08 * SR))
        x[-f:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, f)))
        if not reverse: x[:16] *= np.linspace(0, 1, 16)                        # never a click at the start
    pk = max(np.abs(L).max(), np.abs(R).max())
    g = 10 ** (-1 / 20) / pk
    return L * g, R * g


def write_wav24(path, L, R):
    n = len(L)
    data = bytearray()
    for l, r in zip(np.round(np.clip(L, -1, 1) * 8388607).astype(np.int32), np.round(np.clip(R, -1, 1) * 8388607).astype(np.int32)):
        data += int(l).to_bytes(4, 'little', signed=True)[:3] + int(r).to_bytes(4, 'little', signed=True)[:3]
    fmt = struct.pack('<HHIIHH', 1, 2, SR, SR * 6, 6, 24)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(data)) + bytes(data)
    with open(path, 'wb') as f: f.write(b'RIFF' + struct.pack('<I', len(body)) + body)


def check(name, L, R):
    """objective checks: returns a list of problems"""
    p = []
    if max(np.abs(L).max(), np.abs(R).max()) > 0.95: p.append("clips")
    if abs(L.mean()) > 1e-3 or abs(R.mean()) > 1e-3: p.append("DC")
    if name != "Reverse Bloom" and max(abs(L[0]), abs(R[0])) > 0.01: p.append("click at the start")
    q = len(L) // 4
    e = [np.sqrt(np.mean(L[i * q:(i + 1) * q] ** 2)) for i in range(4)]
    if name == "Reverse Bloom":
        if not e[1] > e[0]: p.append("doesn't swell")
    elif not (e[0] > e[2] > e[3]): p.append("doesn't decay")
    a, b = L[len(L) // 3:], R[len(R) // 3:]
    c = float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-30))
    if name not in ("Glass Void", "Liminal Hz") and abs(c) > 0.5: p.append("not stereo (corr %.2f)" % c)
    return p, c, e


def main():
    out = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else os.path.join(ROOT, "packaging", "factory")
    bad = 0
    for i, (cat, name, secs, fn) in enumerate(FACTORY):
        rng = np.random.default_rng(1000 + i)
        n = int(secs * SR)
        L, R = fn(n, rng)
        L, R = finish(np.asarray(L, float), np.asarray(R, float), reverse=(name == "Reverse Bloom"))
        probs, c, e = check(name, L, R)
        os.makedirs(os.path.join(out, cat), exist_ok=True)
        # file names without spaces (the release tool's rule for install paths); the page shows them with spaces
        write_wav24(os.path.join(out, cat, name.replace(" ", "_") + ".wav"), L, R)
        print("%-8s %-18s %.1f s  L/R corr %+.2f  level by quarter %s  %s" % (cat, name, secs, c,
              " ".join("%.0f" % (20 * np.log10(x + 1e-12)) for x in e), "OK" if not probs else "PROBLEM: " + ", ".join(probs)))
        bad += bool(probs)
    print("%d factory IRs, %d with problems" % (len(FACTORY), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
