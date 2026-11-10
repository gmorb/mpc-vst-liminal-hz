#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""score.py dark|cyber <out dir> -- the trailer's music, synthesized (no samples), as dry stems for the plugin to play.

Two scores on the same 100 BPM, 26-bar plan (62.4 s and a tail), so the same picture cuts fit both:
  dark    D minor (Phrygian): an 80s synth-horror score. A locked octave bass pulse, a string-machine pad, deep drones,
          slow half-step bells, synth toms and a heavy snare, braams on the drop.
  cyber   D minor: cyberpunk. A driven reese bass in 16ths, a locked 16th arpeggio, a distorted kick and a metallic
          snare, data blips, laser zaps and beat-repeat stutters. (The intro of dark is a close heartbeat.)
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

def impact(level=1.0, air_amt=0.25, crack_amt=0.8):
    """a trailer hit: a falling sub boom, a crack, a low tom, a burst of air (air_amt: 0 is tight and close)"""
    n = int(3.5 * SR); t = np.arange(n) / SR
    f = 30 + 70 * np.exp(-t * 3)
    boom = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 1.4)
    crack = hp(rng.standard_normal(n), 2500) * np.exp(-t * 60) * crack_amt
    air = bp(rng.standard_normal(n), 300, 6000) * np.exp(-t * 3.5) * air_amt
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

def heartbeat(level=1.0):
    """two close, dry thumps (lub-dub): a low sine thud with a soft click, no noise tail"""
    n = int(0.7 * SR); out = np.zeros(n)
    for d, g, f0 in ((0.0, 1.0, 62), (0.22, 0.7, 55)):
        i = int(d * SR); m = int(0.35 * SR); t = np.arange(m) / SR
        f = f0 * (1 + 0.6 * np.exp(-t * 30))
        th = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 14) + lp(rng.standard_normal(m), 900) * np.exp(-t * 90) * 0.15
        out[i:i + m] += th * g
    return np.tanh(out * 1.6) * level

def reese(m, dur, level=0.4, cutoff=900):
    """a cyberpunk bass note: two saws a fixed 0.1 semitone apart, a sub, low-passed and driven hard"""
    n = int((dur + 0.02) * SR); t = np.arange(n) / SR; f = hz(m)
    x = saw(f * 2 ** (-0.1 / 12), n, 0.0) + saw(f * 2 ** (0.1 / 12), n, 0.5) + 0.8 * np.sin(2 * np.pi * f / 2 * t)
    return np.tanh(lp(x, cutoff) * 3.0) * env(n, 0.003, 0.03, 0.85, dur, 0.012) * level

def arp(m, dur, level=0.16, cutoff=3200):
    """one arpeggio step: a narrow pulse wave, plucked short, on the grid"""
    n = int((dur + 0.06) * SR); t = np.arange(n) / SR
    ph = (hz(m) * t) % 1.0
    x = np.where(ph < 0.3, 1.0, -1.0)
    return lp(x, cutoff) * np.exp(-t * 14) * (1 - np.exp(-t * 600)) * level

def metal_snare(level=1.0):
    """a hard electronic snare with a ring-modulated metallic edge, clipped"""
    x = snare(1.2); t = np.arange(len(x)) / SR
    return np.clip((x + 0.5 * x * np.sin(2 * np.pi * 1730 * t)) * 1.8, -0.95, 0.95) * level

def blip(m, level=0.2):
    n = int(0.035 * SR); t = np.arange(n) / SR
    return np.sign(np.sin(2 * np.pi * hz(m) * t)) * np.exp(-t * 90) * level

def zap(level=0.3):
    """a laser: a square falling from 2 kHz to 80 Hz in a fifth of a second"""
    n = int(0.25 * SR); t = np.arange(n) / SR
    f = 80 + 1920 * np.exp(-t * 22)
    return lp(np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)), 6000) * np.exp(-t * 9) * level

def stutter(S, key, x, t0, beats, rate=0.125):
    """a beat-repeat: the first slice of x, retriggered every rate beats for beats beats, shorter each time"""
    k = 0
    while k * rate < beats:
        sl = x[: int(rate * BEAT * SR * 0.9)]
        place(S[key], sl * (0.5 + 0.5 * k * rate / beats), t0 + k * rate * BEAT); k += 1

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
    tight = style == "dark"                                       # (dark: close, punchy hits, no burst of air)
    for bar in (6, 18, 23): place(S["hits"], impact(1.0, 0.04 if tight else 0.25, 0.45 if tight else 0.8), t_of(bar))
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
    S = {k: stem() for k in ("drone", "hits", "beat", "tick", "swell", "drums", "pulse", "strings", "bells", "texperc", "sub")}
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
        cutoff = (300 + 150 * (bar - 2)) if bar < 6 else (500 if 16 <= bar < 18 else 1100 if bar < 18 else 1600)
        if bar >= 18: steps = [(k * 0.25, 0.25) for k in range(16)]                       # the drop: straight 16ths
        elif bar >= 6: steps = [(b + o, d) for b in range(4) for o, d in ((0, 0.5), (0.5, 0.25), (0.75, 0.25))]   # a gallop
        else: steps = [(k * 0.5, 0.5) for k in range(8)]                                  # the intro: 8ths
        for k, (b, d) in enumerate(steps):
            m = root + (12 if (k % 2) else 0) - 12
            place(S["pulse"], pulse(m, d * BEAT * 0.8, cutoff, 0.35 if bar >= 6 else 0.22), t_of(bar, b))
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
    for bar in range(0, 6):                                       # the intro: a heartbeat, close and dry, getting louder
        for b in (0, 2):
            if bar == 5 and b == 2: continue
            g = 0.45 + 0.55 * (bar * 2 + b / 2) / 11
            place(S["beat"], heartbeat(g), t_of(bar, b))
    for bar in (6, 23): place(S["hits"], braam(38, 2.2 if bar == 6 else 4.0, 1.0), t_of(bar))
    place(S["bells"], bell(62, 6.0, 0.3), t_of(23))
    silence_before_impacts(S, ("tick", "drums", "texperc", "pulse", "sub", "bells"))
    return S

def build_cyber():
    S = {k: stem() for k in ("drone", "hits", "tick", "swell", "drums", "bass", "arp", "fx", "texperc", "sub")}
    common(S, "cyber")
    CH = [(38, [62, 65, 69, 74]), (34, [58, 62, 65, 70]), (41, [65, 69, 72, 77]), (36, [60, 64, 67, 72])]   # Dm Bb F C
    t_end = t_of(23) + 4.0
    tt = np.arange(int(t_end * SR)) / SR                          # a dark, filtered hum under everything
    hum = (saw(hz(26), len(tt), 0.0) * 0.3 + lp(rng.standard_normal(len(tt)), 200) * 0.3)
    hum = lp(hum, 400) * np.clip(tt / 3.0, 0, 1) * np.clip((t_end - tt) / 3.0, 0, 1)
    place(S["drone"], hum, 0.0)
    # the intro: data blips on 16ths (a fixed pattern), the bass rising through a closing filter
    pat = [1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0]
    notes = [86, 81, 93, 86, 89, 81, 98, 86]
    for bar in range(0, 6):
        for k in range(16):
            if pat[k] and not (bar == 5 and k >= 14):
                place(S["fx"], blip(notes[(bar * 16 + k) % len(notes)], 0.1 + 0.03 * bar), t_of(bar, k * 0.25), pan=0.5 if k % 2 else -0.5)
    for bar in range(2, 6):
        for k in range(16):
            if bar == 5 and k >= 14: continue
            place(S["bass"], reese(38 - 12, 0.25 * BEAT * 0.8, 0.22 + 0.04 * (bar - 2), 250 + 120 * (bar - 2)), t_of(bar, k * 0.25))
    stutter(S, "drums", dist_kick(0.9), t_of(5, 2), 1.5, 0.125)    # a kick stutter into the title
    # the groove (6-13) and the drop (18-22)
    for bar in list(range(6, 14)) + list(range(18, 23)):
        heavy = bar >= 18
        root, chord = CH[(bar - 6) % 4]
        for b in ((0, 1.75, 2.5) if not heavy else (0, 1, 2, 3)): place(S["drums"], dist_kick(1.0 if heavy else 0.85), t_of(bar, b))
        for b in (1, 3): place(S["drums"], metal_snare(1.0), t_of(bar, b))
        for k in range(16): place(S["drums"], hat(level=0.11 if k % 4 == 2 else 0.06), t_of(bar, k * 0.25), pan=0.3 if k % 2 else -0.1)
        for k in range(16):                                       # the bass: 16ths, the root, an octave on the offbeats
            m = root - 12 + (12 if heavy and k % 4 == 2 else 0)
            place(S["bass"], reese(m, 0.25 * BEAT * 0.75, 0.42 if heavy else 0.36, 1300 if heavy else 950), t_of(bar, k * 0.25))
        place(S["sub"], sub_note(root - 12, 4 * BEAT - 0.08, 0.45), t_of(bar))
        seq = [chord[i] for i in (0, 1, 2, 3, 2, 1, 2, 3)] * 2      # the arp: the chord, up and down, 16ths
        for k, m in enumerate(seq):
            place(S["arp"], arp(m + (12 if heavy else 0), 0.25 * BEAT * 0.7, 0.14, 2600 + 1200 * heavy), t_of(bar, k * 0.25),
                  pan=0.35 if k % 2 else -0.35)
        place(S["fx"], zap(0.22 if heavy else 0.15), t_of(bar, 3.5), pan=0.3)
        if bar in (9, 21): stutter(S, "drums", metal_snare(0.8), t_of(bar, 3), 1.0, 0.125)
    for bar in range(16, 18):                                     # the breakdown's second half: the arp, filtered, rising
        root, chord = CH[(bar - 6) % 4]
        for k, m in enumerate([chord[i] for i in (0, 1, 2, 3, 2, 1, 2, 3)] * 2):
            if bar == 17 and k >= 14: continue
            place(S["arp"], arp(m, 0.25 * BEAT * 0.7, 0.12, 900 + 1600 * ((bar - 16) * 16 + k) / 32), t_of(bar, k * 0.25))
    place(S["bass"], reese(26, 3.5, 0.45, 600), t_of(23))
    for k in range(6): place(S["fx"], zap(0.25 - 0.03 * k), t_of(23, 0.5 + k * 0.5), pan=0.4 if k % 2 else -0.4)
    silence_before_impacts(S, ("tick", "drums", "texperc", "bass", "sub", "arp", "fx"))
    return S

def dist_kick(level=1.0):
    return np.tanh(kick(1.0) * 2.4) * 0.92 * level

# the plugin settings per stem (render.sh): factory preset number and key=value, as tools/demos/demo_host takes them
STEMS = {
    "dark": {
        "drone":   "10 mix=70 long_ir=1 feedback=55 decay=200 width=180",      # Glass Void, 20 s, fed back
        "hits":    "4 mix=28 long_ir=1 decay=140 width=160",                   # Backrooms, shorter: impacts, braams
        "beat":    "12 mix=12",                                                # Sub Tunnel, barely: a close heartbeat
        "tick":    "5 mix=45",                                                 # Stairwell Flutter
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "1 mix=22 duck=50",                                         # Empty Mall, ducked
        "pulse":   "12 mix=18",                                                # Sub Tunnel: a dark room, a touch
        "strings": "14 mix=55 long_ir=1",                                      # Liminal Hz
        "bells":   "10 mix=65 long_ir=1 decay=180",                            # Glass Void
        "texperc": "17 mix=75",                                                # Tuned Chord: drums that play a chord
        "sub":     None,                                                       # (dry)
    },
    "cyber": {
        "drone":   "12 mix=50",                                                # Sub Tunnel: the hum
        "hits":    "10 mix=35 decay=120",                                      # Glass Void, short
        "tick":    "9 mix=45",                                                 # Intercom Echo
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "8 mix=12 duck=30",                                         # Parking Slap: tight, a touch of slap
        "bass":    "12 mix=8",                                                 # Sub Tunnel, barely
        "arp":     "2 mix=25 width=200",                                       # Pool Rooms, wide
        "fx":      "7 mix=50 predelay_sync=1 predelay=125 bpm=100",            # Dream Ping-Pong, 1/16 echoes
        "texperc": "17 mix=75",                                                # Tuned Chord
        "sub":     None,
    },
}
LEVEL = {   # dB against the drums, by loudness while each plays (mix.py)
    "dark": {"drums": 0.0, "hits": 0.0, "beat": -1.0, "pulse": -3.0, "sub": -5.0, "texperc": -2.0, "swell": -5.0, "strings": -9.0,
             "bells": -8.0, "drone": -8.0, "tick": -15.0},
    "cyber": {"drums": 0.0, "hits": -1.0, "bass": -2.0, "sub": -6.0, "arp": -9.0, "fx": -12.0, "texperc": -2.0,
              "swell": -5.0, "drone": -10.0, "tick": -16.0},
}
BUILD = {"dark": build_dark, "cyber": build_cyber}

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
