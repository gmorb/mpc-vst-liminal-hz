#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""score.py dark|bright <out dir> -- the trailer's music, synthesized (no samples), as dry stems for the plugin to play.

Two scores on the same 100 BPM, 26-bar plan (62.4 s and a tail), so the same picture cuts fit both:
  dark    D minor (Phrygian): an 80s synth-horror score. A locked octave bass pulse, a string-machine pad, deep drones,
          slow half-step bells, synth toms and a heavy snare, braams on the drop.
  bright  D major: clean and modern. Wide held pads that breathe with the kick, a simple held lead, punchy
          four-on-the-floor drums, a clean sub.
Nothing is played loosely: every note is on the grid, with fixed (not random) tuning spreads. The plan:
  bars  0-3   intro: drones, a clock, (dark) the pulse creeping in
  bars  4-5   a reverse swell, the clock doubling, then half a beat of silence
  bar   6     IMPACT (the title); bars 6-13 the groove
  bars 14-17  breakdown: drums through Tuned Chord, a riser, a snare roll, silence
  bar  18     IMPACT (the drop), bars 18-22; bar 23 the last IMPACT, everything rings out
Each stem is a 44.1 kHz stereo float WAV; STEMS[style] says which factory preset and settings Liminal Hz plays it
through (tools/trailer/render.sh), LEVEL[style] (mix.py) how loud it sits against the drums."""
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

def sub_note(m, dur, level=0.5):
    n = int((dur + 0.1) * SR); t = np.arange(n) / SR
    x = np.sin(2 * np.pi * hz(m) * t) + 0.25 * np.sin(2 * np.pi * hz(m) * 2 * t)
    return np.tanh(x * 1.3) * env(n, 0.01, 0.1, 0.9, dur, 0.06) * level

# ---- more sounds for the two scores -------------------------------------------------------------------------------
def pulse(m, dur, cutoff=900, level=0.4):
    """one note of a sequenced synth bass: a saw and a square an octave down, through a low-pass that snaps shut"""
    n = int((dur + 0.05) * SR); t = np.arange(n) / SR
    f = hz(m)
    x = saw(f, n, 0.0) * 0.7 + np.sign(np.sin(2 * np.pi * f / 2 * t)) * 0.35
    y = np.zeros(n); seg = 512
    for i in range(0, n, seg):                                   # the filter: cutoff x (1 + 3 e^-t/40ms)
        fc = min(SR / 2.5, cutoff * (1 + 3 * np.exp(-t[i] / 0.04)))
        y[i:i + seg] = lp(x[i:i + seg], fc)
    return np.tanh(y * 1.5) * env(n, 0.002, 0.05, 0.75, dur, 0.02) * level

def strings(notes, dur, level=0.1, cutoff=1600):
    """a string machine: saws with a small fixed spread, slow in and out"""
    n = int((dur + 1.2) * SR); out = np.zeros((n, 2))
    for m in notes:
        for ch, det in ((0, -0.02), (1, 0.02)):
            out[:, ch] += saw(hz(m) * 2 ** (det / 12), n, 0.25 * ch)
    e = env(n, 1.0, 0.5, 0.9, dur, 0.9)
    return np.stack([lp(out[:, 0], cutoff), lp(out[:, 1], cutoff)], 1) * e[:, None] * level

def bell(m, dur=3.0, level=0.25):
    """a clean FM bell: a sine with a 3.5x modulator that fades, long and still"""
    n = int(dur * SR); t = np.arange(n) / SR; f = hz(m)
    mod = np.sin(2 * np.pi * f * 3.5 * t) * 2.2 * np.exp(-t * 4)
    return np.sin(2 * np.pi * f * t + mod) * np.exp(-t * 1.1) * (1 - np.exp(-t * 300)) * level

def syn_tom(m, level=0.7):
    """an 80s electronic tom: a sine falling in pitch, a puff of noise"""
    n = int(0.6 * SR); t = np.arange(n) / SR
    f = hz(m) * (1 + 1.2 * np.exp(-t * 18))
    return (np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 5) + lp(rng.standard_normal(n), 3000) * np.exp(-t * 40) * 0.15) * level

def big_snare(level=1.0):
    """a heavy snare with a short burst of its own room, cut off hard (gated)"""
    x = snare(1.4); n = int(0.42 * SR); out = np.zeros(n); out[: len(x)] += x[:n]
    out += bp(rng.standard_normal(n), 800, 7000) * 0.35 * (np.arange(n) < int(0.3 * SR))
    return np.tanh(out * 1.2) * level

def clap(level=0.8):
    n = int(0.3 * SR); t = np.arange(n) / SR; y = np.zeros(n)
    for d in (0.0, 0.008, 0.016, 0.026):
        i = int(d * SR); y[i:] += bp(rng.standard_normal(n - i), 1000, 6000) * np.exp(-t[: n - i] * 35)
    return y * level * 0.5

def tight_kick(level=1.0):
    n = int(0.45 * SR); t = np.arange(n) / SR
    f = 48 + 140 * np.exp(-t * 45)
    return np.tanh((np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7) + hp(rng.standard_normal(n), 4000) * np.exp(-t * 500) * 0.3) * 2.0) * 0.9 * level

def wide_pad(notes, dur, level=0.07):
    """a modern wide pad: three saws a note at fixed -7/0/+7 cents, high-passed, bright but soft"""
    n = int((dur + 0.8) * SR); out = np.zeros((n, 2))
    for k, m in enumerate(notes):
        for ch in range(2):
            for det in (-0.07, 0.0, 0.07):
                out[:, ch] += saw(hz(m) * 2 ** ((det + (0.01 if ch else -0.01)) / 12), n, (k * 0.13 + ch * 0.5 + det) % 1)
    e = env(n, 0.35, 0.3, 0.9, dur, 0.5)
    y = np.stack([lp(hp(out[:, c], 180), 3800) for c in range(2)], 1)   # (bright, not buzzy)
    return y * e[:, None] * level

def lead(m, dur, level=0.18):
    """a clean held lead: a soft triangle with a little vibrato once the note settles"""
    n = int((dur + 0.3) * SR); t = np.arange(n) / SR
    f = hz(m) * (1 + 0.004 * np.sin(2 * np.pi * 5 * t) * np.clip((t - 0.35) / 0.3, 0, 1))
    ph = np.cumsum(f) / SR
    tri = 2 * np.abs(2 * (ph % 1.0) - 1) - 1
    return lp(tri + 0.15 * np.sin(2 * np.pi * ph * 2), 5000) * env(n, 0.03, 0.2, 0.85, dur, 0.25) * level

def pump(n_total, beats, depth=0.6, rel=0.28):
    """a kick-shaped volume dip (as a sidechain does): 1 - depth at each beat, back over rel seconds"""
    g = np.ones(n_total); t = np.arange(n_total) / SR
    for b in beats:
        i = int(b * SR); j = min(n_total, i + int(rel * 1.5 * SR))
        g[i:j] = np.minimum(g[i:j], 1 - depth * np.exp(-(t[i:j] - b) / (rel / 3)))
    return g

# ---- the arrangements ----------------------------------------------------------------------------------------------
def common(S, style):
    """what both share: the clock, the swells and riser, the impacts, the breakdown through Tuned Chord, the roll"""
    for k in range(8 * 4):
        place(S["tick"], tick(0.12 + 0.15 * k / 32), t_of(0, k * 0.5), pan=0.4 if k % 2 else -0.4)
    for k in range(8 * 2 - 1):
        place(S["tick"], tick(0.3), t_of(4, k * 0.25), pan=0.3 if k % 2 else -0.3)
    place(S["swell"], reverse_swell(2 * BAR - 0.3, 0.9), t_of(4))
    place(S["swell"], riser(2 * BAR - 0.3, 0.7), t_of(16))
    place(S["swell"], reverse_swell(BAR - 0.3, 0.8), t_of(22))
    for bar in (6, 18, 23): place(S["hits"], impact(1.0), t_of(bar))
    for bar in range(14, 18):
        for b in (0, 1.5, 2.5): place(S["texperc"], kick(0.4) * 0.8, t_of(bar, b))
        for b in (1, 3): place(S["texperc"], snare(0.8), t_of(bar, b))
        for k in range(8): place(S["texperc"], hat(level=0.1), t_of(bar, k * 0.5), pan=0.2)
    for k in range(16):                                           # bar 17: 16ths, then 32nds, crescendo
        b = (k * 0.25) if k < 8 else (2 + (k - 8) * 0.125)
        place(S["drums"], snare(0.6) * (0.3 + 0.7 * k / 16), t_of(17, b))

def silence_before_impacts(S, keys):
    for bar in (6, 18):
        a, b = int((t_of(bar) - BEAT * 0.5) * SR), int(t_of(bar) * SR)
        for k in keys: S[k][a:b] *= np.linspace(1, 0, b - a)[:, None] if k == "drums" else 0

def build_dark():
    S = {k: stem() for k in ("drone", "hits", "tick", "swell", "drums", "pulse", "strings", "bells", "texperc", "sub")}
    common(S, "dark")
    ROOTS = [38, 38, 39, 36]                                      # D, D, Eb, C: the half step up is the menace
    PADS = [[50, 53, 57], [50, 53, 57], [51, 55, 58], [48, 51, 55]]   # Dm, Dm, Eb, Cm
    t_end = t_of(23) + 4.0
    tt = np.arange(int(t_end * SR)) / SR                          # the drone: a sub on D and dark air, all the way
    drone = np.sin(2 * np.pi * hz(26) * tt) * 0.35 + lp(rng.standard_normal(len(tt)), 90) * 0.4
    drone *= np.clip(tt / 4.0, 0, 1) * np.clip((t_end - tt) / 3.0, 0, 1)
    place(S["drone"], drone, 0.0)
    for bar in (0, 2, 4, 14, 16):
        place(S["strings"], strings([50, 53, 57] if bar % 4 == 0 else [51, 55, 58], 2 * BAR - 0.3, 0.09, 900), t_of(bar))
    # the pulse: octave 8ths, creeping in through a closed filter (bars 2-5), then locked to the chords
    for bar in range(2, 23):
        if 14 <= bar < 16: continue                               # (the breakdown's first half: pulse out)
        root = ROOTS[(bar - 6) % 4] if bar >= 6 else 38
        sixteenths = bar >= 18
        step = 0.25 if sixteenths else 0.5
        cutoff = (300 + 150 * (bar - 2)) if bar < 6 else (500 if 16 <= bar < 18 else 1100 if bar < 18 else 1600)
        for k in range(int(4 / step)):
            m = root + (12 if (k % 2) else 0) - 12
            place(S["pulse"], pulse(m, step * BEAT * 0.8, cutoff, 0.35 if bar >= 6 else 0.22), t_of(bar, k * step))
    for bar in list(range(6, 14)) + list(range(18, 23)):
        heavy = bar >= 18
        root, pad = ROOTS[(bar - 6) % 4], PADS[(bar - 6) % 4]
        if (bar - 6) % 2 == 0: place(S["strings"], strings(pad + [pad[0] + 12], 2 * BAR - 0.2, 0.08, 1400), t_of(bar))
        for b in ((0, 2) if not heavy else (0, 1, 2, 3)): place(S["drums"], kick(0.9 if heavy else 0.7), t_of(bar, b))
        for b in (1, 3): place(S["drums"], big_snare(1.0 if heavy else 0.85), t_of(bar, b))
        for k in range(8): place(S["drums"], hat(level=0.06 if k % 2 else 0.09), t_of(bar, k * 0.5), pan=0.25)
        if bar in (9, 13, 21):                                    # tom fills, on the grid
            for k, m in enumerate((52, 48, 45, 41)): place(S["drums"], syn_tom(m), t_of(bar, 2 + k * 0.5), pan=0.5 - 0.33 * k)
        place(S["sub"], sub_note(root - 12 + 12, 4 * BEAT - 0.1, 0.5 if heavy else 0.4), t_of(bar))
        if heavy: place(S["hits"], braam(root, 1.8, 0.75), t_of(bar))
    for bar, (a, b) in ((6, (69, 70)), (8, (69, 70)), (10, (72, 70)), (12, (69, 68))):   # the bells: two slow notes
        place(S["bells"], bell(a, 2.8, 0.22), t_of(bar))
        place(S["bells"], bell(b, 2.8, 0.22), t_of(bar + 1))
    for bar in (18, 20):
        place(S["bells"], bell(81, 3.0, 0.2), t_of(bar)); place(S["bells"], bell(82, 3.0, 0.2), t_of(bar + 1))
    place(S["hits"], braam(38, 2.0, 0.55), t_of(0)); place(S["hits"], braam(39, 2.0, 0.6), t_of(2))
    for bar in (6, 23): place(S["hits"], braam(38, 2.2 if bar == 6 else 4.0, 1.0), t_of(bar))
    place(S["bells"], bell(62, 6.0, 0.3), t_of(23))
    silence_before_impacts(S, ("tick", "drums", "texperc", "pulse", "sub", "bells"))
    return S

def build_bright():
    S = {k: stem() for k in ("drone", "hits", "tick", "swell", "drums", "pad", "lead", "texperc", "sub")}
    common(S, "bright")
    CH = [(38, [62, 66, 69, 74]), (33, [61, 64, 69, 73]), (35, [62, 66, 71, 74]), (31, [62, 67, 71, 74])]   # D A Bm G
    t_end = t_of(23) + 4.0
    tt = np.arange(int(t_end * SR)) / SR                          # a clean drone: D and A, high and soft
    drone = (np.sin(2 * np.pi * hz(62) * tt) + 0.6 * np.sin(2 * np.pi * hz(69) * tt) + 0.3 * np.sin(2 * np.pi * hz(74) * tt)) * 0.08
    drone *= np.clip(tt / 5.0, 0, 1) * np.clip((t_end - tt) / 3.0, 0, 1) * (1 - 0.6 * ((tt > t_of(6)) & (tt < t_of(23))))
    place(S["drone"], drone, 0.0)
    for bar in (0, 2, 4, 14, 16):                                 # the intro and breakdown: the pad alone, held
        root, notes = CH[0] if bar % 4 == 0 else CH[3]
        place(S["pad"], wide_pad(notes, 2 * BAR - 0.3, 0.05), t_of(bar))
    MEL = [[(78, 0, 2), (76, 2, 1), (74, 3, 1)], [(76, 0, 4)], [(78, 0, 2), (81, 2, 2)], [(79, 0, 3), (74, 3, 1)]]
    kicks = []
    for bar in list(range(6, 14)) + list(range(18, 23)):
        heavy = bar >= 18
        root, notes = CH[(bar - 6) % 4]
        pad = wide_pad(notes, BAR - 0.05, 0.065 if heavy else 0.055)
        bk = (0, 1, 2, 3) if heavy else (0, 2.5)
        g = pump(len(pad), [b * BEAT for b in (0, 1, 2, 3)], 0.55 if heavy else 0.35)
        place(S["pad"], pad * g[:, None], t_of(bar))
        for b in bk: place(S["drums"], tight_kick(1.0 if heavy else 0.8), t_of(bar, b)); kicks.append(t_of(bar, b))
        for b in (1, 3): place(S["drums"], clap(0.9), t_of(bar, b))
        for k in range(8): place(S["drums"], hat(level=0.1 if k % 2 else 0.05), t_of(bar, k * 0.5 + (0.0 if k % 2 else 0)), pan=0.3)
        if heavy:
            for k in range(4): place(S["drums"], hat(True, 0.07), t_of(bar, k + 0.5), pan=-0.25)
        sub = sub_note(root, 4 * BEAT - 0.08, 0.5 if heavy else 0.42)
        place(S["sub"], sub * pump(len(sub), [b * BEAT for b in (0, 1, 2, 3)], 0.5, 0.2), t_of(bar))
        for m, b, d in MEL[(bar - 6) % 4]:
            if bar >= 6 and (bar < 14 or heavy): place(S["lead"], lead(m + (12 if heavy and bar >= 20 else 0), d * BEAT - 0.05), t_of(bar, b))
        if bar in (9, 13):
            for k, m in enumerate((55, 52, 48, 45)): place(S["drums"], syn_tom(m, 0.5), t_of(bar, 2 + k * 0.5), pan=0.4 - 0.27 * k)
    place(S["pad"], wide_pad(CH[0][1] + [78], 5.0, 0.07), t_of(23))
    place(S["lead"], lead(86, 3.5, 0.16), t_of(23))
    silence_before_impacts(S, ("tick", "drums", "texperc", "sub", "lead", "pad"))
    return S

# the plugin settings per stem (render.sh): factory preset number and key=value, as tools/demos/demo_host takes them
STEMS = {
    "dark": {
        "drone":   "10 mix=70 long_ir=1 feedback=55 decay=200 width=180",      # Glass Void, 20 s, fed back
        "hits":    "4 mix=40 long_ir=1 decay=250 width=180",                   # Backrooms, long: impacts, braams
        "tick":    "5 mix=45",                                                 # Stairwell Flutter
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "1 mix=22 duck=50",                                         # Empty Mall, ducked
        "pulse":   "12 mix=18",                                                # Sub Tunnel: a dark room, a touch
        "strings": "14 mix=55 long_ir=1",                                      # Liminal Hz
        "bells":   "10 mix=65 long_ir=1 decay=180",                            # Glass Void
        "texperc": "17 mix=75",                                                # Tuned Chord: drums that play a chord
        "sub":     None,                                                       # (dry)
    },
    "bright": {
        "drone":   "13 mix=60",                                                # Shimmer Fog
        "hits":    "3 mix=40 long_ir=1 decay=150 width=180",                   # Fluorescent Hall, long
        "tick":    "2 mix=40",                                                 # Pool Rooms
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "2 mix=16 duck=40",                                         # Pool Rooms, bright and ducked
        "pad":     "3 mix=40 duck=60",                                         # Fluorescent Hall, Duck clears it
        "lead":    "13 mix=45",                                                # Shimmer Fog
        "texperc": "17 mix=75",                                                # Tuned Chord
        "sub":     None,
    },
}
LEVEL = {   # dB against the drums, by loudness while each plays (mix.py)
    "dark": {"drums": 0.0, "hits": 1.0, "pulse": -3.0, "sub": -5.0, "texperc": -2.0, "swell": -5.0, "strings": -9.0,
             "bells": -8.0, "drone": -8.0, "tick": -15.0},
    "bright": {"drums": 0.0, "hits": -1.0, "pad": -7.0, "lead": -6.0, "sub": -4.0, "texperc": -2.0, "swell": -5.0,
               "drone": -12.0, "tick": -16.0},
}
BUILD = {"dark": build_dark, "bright": build_bright}

def write(path, x):
    x = np.asarray(x, dtype=np.float32)
    data = x.astype("<f4").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + (36 + len(data)).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little"))
        f.write((3).to_bytes(2, "little") + (2).to_bytes(2, "little") + SR.to_bytes(4, "little") + (SR * 8).to_bytes(4, "little"))
        f.write((8).to_bytes(2, "little") + (32).to_bytes(2, "little") + b"data" + len(data).to_bytes(4, "little") + data)

if __name__ == "__main__":
    style, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    for k, x in BUILD[style]().items():
        m = np.max(np.abs(x))
        write(os.path.join(out, k + ".wav"), x * (0.7 / m if m > 0 else 1))
        print(k, "peak %.2f" % m)
