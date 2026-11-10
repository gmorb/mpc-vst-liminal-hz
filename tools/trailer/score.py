#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""score.py <out dir> -- the trailer's music, synthesized (no samples), as dry stems for the plugin to process.

100 BPM, D minor, 26 bars (62.4 s) and a tail. Each stem is a 44.1 kHz stereo float WAV of the same length; STEMS says
which factory preset and settings Liminal Hz plays it through (tools/trailer/render.sh), and MIX how loud it sits.
  bars  0-3   intro: a sub drone and dark pad, clock ticks, two soft braams
  bars  4-5   a reverse swell, the ticks doubling, then half a beat of silence
  bar   6     IMPACT (the title)
  bars  6-13  the groove: drums, sub bass, chord stabs (SYNC'd echoes)
  bars 14-17  breakdown: drums through Tuned Chord, a riser, a snare roll, silence
  bar  18     IMPACT (the drop), bars 18-22 heavy drums, braams on every bar
  bar  23     the last IMPACT; everything rings out
"""
import os, sys
import numpy as np
from scipy.signal import butter, sosfilt

SR, BPM = 44100, 100
BEAT = 60 / BPM; BAR = 4 * BEAT
BARS = 26
TOTAL = BARS * BAR + 5.0
N = int(TOTAL * SR)
rng = np.random.default_rng(1414)

def t_of(bar, beat=0.0): return bar * BAR + beat * BEAT
def hz(m): return 440.0 * 2 ** ((m - 69) / 12)
def lp(x, fc, order=2): return sosfilt(butter(order, fc, "low", fs=SR, output="sos"), x)
def hp(x, fc, order=2): return sosfilt(butter(order, fc, "high", fs=SR, output="sos"), x)
def bp(x, lo, hi, order=2): return sosfilt(butter(order, [lo, hi], "band", fs=SR, output="sos"), x)

def stem(): return np.zeros((N, 2))
def place(out, x, t, gain=1.0, pan=0.0):
    i = int(t * SR)
    if x.ndim == 1: x = np.stack([x * (1 - max(0, pan)), x * (1 + min(0, pan))], 1)
    x = x[: max(0, N - i)]
    out[i:i + len(x)] += gain * x

def saw(f, n, phase=None):
    ph = np.cumsum(np.broadcast_to(f, (n,))) / SR + (rng.random() if phase is None else phase)
    return 2 * (ph % 1.0) - 1

def env(n, a, d, s, rel_at, r):
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-4), np.where(t < a + d, 1 - (1 - s) * (t - a) / max(d, 1e-4), s))
    k = t >= rel_at; e[k] = e[k] * np.exp(-(t[k] - rel_at) / r)
    return e

# ---- sounds -------------------------------------------------------------------------------------------------------
def kick(hard=1.0):
    n = int(0.6 * SR); t = np.arange(n) / SR
    f = 42 + 120 * np.exp(-t * 35)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * (5.5 - 1.5 * hard))
    click = hp(rng.standard_normal(n), 3000) * np.exp(-t * 400) * 0.35
    return np.tanh((body + click) * (1.3 + 0.8 * hard)) * 0.9

def snare(big=1.0):
    n = int(0.45 * SR); t = np.arange(n) / SR
    body = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 20) * 0.5 + np.sin(2 * np.pi * 330 * t) * np.exp(-t * 28) * 0.25
    noise = bp(rng.standard_normal(n), 1200, 9000) * np.exp(-t * (14 / big)) * 0.7
    clap = np.zeros(n)
    for k, d in enumerate((0.0, 0.011, 0.022)):                   # a clap layer: three quick bursts
        i = int(d * SR); m = int(0.02 * SR)
        clap[i:i + m] += bp(rng.standard_normal(m), 900, 4000) * np.exp(-np.arange(m) / SR * 150) * 0.5
    return np.tanh((body + noise + clap * big) * 1.4) * 0.8

def hat(open_=False, level=0.15):
    n = int((0.4 if open_ else 0.07) * SR); t = np.arange(n) / SR
    return hp(rng.standard_normal(n), 7500, 4) * np.exp(-t * (9 if open_ else 70)) * level

def tom(m, level=0.6):
    n = int(0.5 * SR); t = np.arange(n) / SR
    f = hz(m) * (1 + 0.4 * np.exp(-t * 30))
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 8) * level

def tick(level=0.3):                          # a clock's tick: a short knock of filtered noise (no pure tone to ring)
    n = int(0.05 * SR); t = np.arange(n) / SR
    return bp(rng.standard_normal(n), 1500, 7000) * np.exp(-t * 180) * level * 1.6

def braam(root=38, dur=2.2, level=1.0):
    """a low brass-like blast: detuned saw stacks, the filter opening and closing, driven"""
    n = int((dur + 0.5) * SR); t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for m in (root - 12, root, root + 7, root + 12):           # octaves and a fifth: one solid chord, not a cluster
        for ch in range(2):
            for det in ((-0.035, 0.035) if ch == 0 else (-0.025, 0.045)):   # a slight, fixed spread (stereo width)
                out[:, ch] += saw(hz(m) * 2 ** (det / 12), n, phase=0.0) * (1.4 if m < root else 1.0)
    cut = 180 + 2600 * np.exp(-t * 2.2) * (1 - np.exp(-t * 40))
    y = np.zeros_like(out)
    blocks = 256                                                 # a swept low-pass: filtered in blocks, overlapped
    for ch in range(2):
        state = None
        for i in range(0, n, blocks):
            sos = butter(2, min(cut[i], SR / 2.2), "low", fs=SR, output="sos")
            if state is None: state = np.zeros((sos.shape[0], 2))
            from scipy.signal import sosfilt as _s
            seg, state = _s(sos, out[i:i + blocks, ch], zi=state)
            y[i:i + blocks, ch] = seg
    e = env(n, 0.03, 0.4, 0.7, dur, 0.35)
    return np.tanh(y * e[:, None] * 0.35) * level

def impact(level=1.0):
    """a trailer hit: a falling sub boom, a crack, a low tom, a burst of air"""
    n = int(3.5 * SR); t = np.arange(n) / SR
    f = 30 + 70 * np.exp(-t * 3)
    boom = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 1.4)
    crack = hp(rng.standard_normal(n), 2500) * np.exp(-t * 60) * 0.8
    air = bp(rng.standard_normal(n), 300, 6000) * np.exp(-t * 3.5) * 0.25
    low = np.sin(2 * np.pi * 73 * t) * np.exp(-t * 6) * 0.5
    x = np.tanh((boom * 1.5 + crack + air + low) * 1.2) * level
    return np.stack([x, x * 0.97], 1)

def reverse_swell(dur, level=0.8):
    """a cymbal-like wash played backwards: rising to the hit"""
    n = int(dur * SR); t = np.arange(n) / SR
    wash = np.stack([hp(rng.standard_normal(n), 2500), hp(rng.standard_normal(n), 2500)], 1)
    tone = np.sin(2 * np.pi * hz(62) * t)[:, None] * 0.3 + np.sin(2 * np.pi * hz(69) * t)[:, None] * 0.2
    e = (np.exp(-(dur - t) * 2.2))[:, None]
    return (wash * 0.5 + tone) * e * level

def riser(dur, level=0.6):
    n = int(dur * SR); t = np.arange(n) / SR
    f0 = hz(50) * 2 ** (3 * (t / dur) ** 2)                      # three octaves up, faster at the end
    s = saw(f0, n) * 0.8                                        # (one voice: a clean rise)
    noise = rng.standard_normal(n)
    y = np.zeros(n)
    for i in range(0, n, 2048):                                  # the noise band follows the pitch
        fc = min(16000, 400 + 9000 * (t[i] / dur) ** 2)
        y[i:i + 2048] = bp(noise[i:i + 2048], fc * 0.7, min(fc * 1.4, 20000))
    e = (t / dur) ** 2
    x = (lp(s, 1200 + 6000 * 1.0) * 0.6 + y * 0.7) * e * level
    return np.stack([x, np.roll(x, 37)], 1)

def pad_chord(notes, dur, level=0.12):
    n = int((dur + 1.0) * SR)
    out = np.zeros((n, 2))
    for m in notes:
        for ch in range(2):
            out[:, ch] += saw(hz(m) * 2 ** ((0.03 if ch else -0.03) / 12), n)
    e = env(n, 0.8, 0.5, 0.85, dur, 0.6)
    return np.stack([lp(out[:, 0], 700), lp(out[:, 1], 700)], 1) * e[:, None] * level

def stab(notes, level=0.18):
    n = int(0.5 * SR)
    out = np.zeros((n, 2))
    for m in notes:
        for ch in range(2):
            out[:, ch] += saw(hz(m) * 2 ** ((0.02 if ch else -0.02) / 12), n)
    e = env(n, 0.003, 0.09, 0.4, 0.18, 0.05)
    return np.stack([lp(out[:, 0], 3200), lp(out[:, 1], 3200)], 1) * e[:, None] * level

def sub_note(m, dur, level=0.5):
    n = int((dur + 0.1) * SR); t = np.arange(n) / SR
    x = np.sin(2 * np.pi * hz(m) * t) + 0.25 * np.sin(2 * np.pi * hz(m) * 2 * t)
    return np.tanh(x * 1.3) * env(n, 0.01, 0.1, 0.9, dur, 0.06) * level

# ---- the arrangement -----------------------------------------------------------------------------------------------
CHORDS = [  # per bar of the groove and the drop: (sub root, stab voicing)
    (38, [62, 65, 69, 72, 76]),          # Dm9
    (34, [58, 62, 65, 69, 72]),          # Bbmaj7(9)
    (31, [55, 58, 62, 65, 69]),          # Gm9
    (33, [57, 61, 64, 67, 71]),          # A7(9)
]

def build():
    S = {k: stem() for k in ("drone", "braam", "tick", "swell", "drums", "stabs", "texperc", "sub")}
    # intro + breakdown: the drone (a sub on D, a dark pad moving Dm -> Bb)
    t_end = t_of(18)
    tt = np.arange(int(t_end * SR)) / SR
    drone = np.sin(2 * np.pi * hz(26) * tt) * 0.35 + lp(rng.standard_normal(len(tt)), 90) * 0.4
    drone *= np.clip(tt / 4.0, 0, 1) * (1 - 0.6 * ((tt > t_of(6)) & (tt < t_of(14))))   # under the groove: lower
    place(S["drone"], drone, 0.0)
    for bar, notes in ((0, [50, 53, 57]), (2, [46, 50, 53]), (4, [50, 53, 57, 60]), (14, [50, 53, 57]),
                       (16, [46, 50, 53, 57])):
        place(S["drone"], pad_chord(notes, 2 * BAR - 0.3), t_of(bar))
    # the clock: 8ths, then 16ths, getting louder; a breath of silence before each impact
    for k in range(8 * 4):
        place(S["tick"], tick(0.12 + 0.15 * k / 32), t_of(0, k * 0.5), pan=0.4 if k % 2 else -0.4)
    for k in range(8 * 2 - 1):
        place(S["tick"], tick(0.3), t_of(4, k * 0.25), pan=0.3 if k % 2 else -0.3)
    # braams and impacts
    place(S["braam"], braam(38, 2.0, 0.55), t_of(0))
    place(S["braam"], braam(34, 2.0, 0.65), t_of(2))
    for bar in (6, 18, 23):
        place(S["braam"], impact(1.0), t_of(bar))
        place(S["braam"], braam(38, 2.2 if bar != 23 else 4.0, 1.0), t_of(bar))
    for bar in (10,):
        place(S["braam"], braam(34, 1.8, 0.7), t_of(bar))
    for bar in range(19, 23):
        place(S["braam"], braam(CHORDS[bar % 4][0] + 12, 1.6, 0.8), t_of(bar))
    # the swells: into the title, and a riser into the drop
    place(S["swell"], reverse_swell(2 * BAR - 0.3, 0.9), t_of(4))
    place(S["swell"], riser(2 * BAR - 0.3, 0.7), t_of(16))
    place(S["swell"], reverse_swell(BAR - 0.3, 0.8), t_of(22))
    # the groove (bars 6-13) and the drop (18-22)
    for bar in list(range(6, 14)) + list(range(18, 23)):
        heavy = bar >= 18
        root, voicing = CHORDS[(bar - 6) % 4]
        for b in ((0, 1.5, 2.5) if not heavy else (0, 0.75, 1.5, 2.5, 3.25)): place(S["drums"], kick(1.0 if heavy else 0.6), t_of(bar, b))
        for b in (1, 3): place(S["drums"], snare(1.3 if heavy else 1.0), t_of(bar, b))
        for k in range(16):
            if bar == 6 and k < 4: continue
            place(S["drums"], hat(level=(0.14 if k % 4 == 2 else 0.07) * (1.3 if heavy else 1)), t_of(bar, k * 0.25), pan=0.3)
        if heavy: place(S["drums"], hat(True, 0.12), t_of(bar, 3.5), pan=-0.2)
        if bar in (13,):                                          # a fill into the breakdown
            for k, m in enumerate((50, 47, 45, 43)): place(S["drums"], tom(m), t_of(bar, 2 + k * 0.5), pan=0.4 - 0.25 * k)
        for b, d in ((0, 1.4), (1.5, 0.9), (2.5, 1.4)): place(S["sub"], sub_note(root, d * BEAT, 0.55 if heavy else 0.45), t_of(bar, b))
        for b in ((0.5, 2.75) if not heavy else (0.5, 1.75, 2.75, 3.5)):
            place(S["stabs"], stab(voicing, 0.16), t_of(bar, b))
    # breakdown: drums through Tuned Chord, and a snare roll into the drop
    for bar in range(14, 18):
        for b in (0, 1.5, 2.5): place(S["texperc"], kick(0.4) * 0.8, t_of(bar, b))
        for b in (1, 3): place(S["texperc"], snare(0.8), t_of(bar, b))
        for k in range(8): place(S["texperc"], hat(level=0.1), t_of(bar, k * 0.5), pan=0.2)
    for k in range(16):                                           # bar 17: 16ths, then 32nds, crescendo
        step = 0.25 if k < 8 else 0.125
        b = (k * 0.25) if k < 8 else (2 + (k - 8) * 0.125)
        place(S["drums"], snare(0.6) * (0.3 + 0.7 * k / 16), t_of(17, b))
    # silence before each impact: everything but the swells stops half a beat early
    for bar in (6, 18):
        a, b = int((t_of(bar) - BEAT * 0.5) * SR), int(t_of(bar) * SR)
        for k in ("tick", "drums", "texperc", "stabs", "sub"): S[k][a:b] *= np.linspace(1, 0, b - a)[:, None] if k == "drums" else 0
    return S

# the plugin settings per stem (render.sh): factory preset number and key=value, as tools/demos/demo_host takes them
STEMS = {
    "drone":   "10 mix=70 long_ir=1 feedback=55 decay=200 width=180",          # Glass Void, 20 s, fed back
    "braam":   "4 mix=40 long_ir=1 decay=250 width=180",                       # Backrooms, long
    "tick":    "5 mix=45",                                                     # Stairwell Flutter
    "swell":   "11 mix=70",                                                    # Reverse Bloom
    "drums":   "1 mix=22 duck=50",                                             # Empty Mall, ducked
    "stabs":   "7 mix=50 predelay_sync=1 predelay=225 bpm=100",                # Dream Ping-Pong, 1/8 D echoes
    "texperc": "17 mix=75",                                                    # Tuned Chord: drums that play a chord
    "sub":     None,                                                           # (dry)
}
MIX = {"drone": 0.9, "braam": 0.9, "tick": 0.8, "swell": 0.8, "drums": 1.0, "stabs": 0.8, "texperc": 0.9, "sub": 0.8}

def write(path, x):
    x = np.asarray(x, dtype=np.float32)
    data = x.astype("<f4").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + (36 + len(data)).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little"))
        f.write((3).to_bytes(2, "little") + (2).to_bytes(2, "little") + SR.to_bytes(4, "little") + (SR * 8).to_bytes(4, "little"))
        f.write((8).to_bytes(2, "little") + (32).to_bytes(2, "little") + b"data" + len(data).to_bytes(4, "little") + data)

if __name__ == "__main__":
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    for k, x in build().items():
        m = np.max(np.abs(x))
        write(os.path.join(out, k + ".wav"), x * (0.7 / m if m > 0 else 1))
        print(k, "peak %.2f" % m)
