#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""score.py dark|cyber|trap <out dir> -- the trailer's music, synthesized (no samples), as dry stems for the plugin to play.

Three different songs (the style comes from argv, or TRAILER_STYLE when imported; it sets the tempo and the meter):
  dark    120 BPM in 5/4 (3+2), D minor, 26 bars (65 s): an 80s synth-horror score. A round sub ostinato (sine and
          triangle, never brighter than ~700 Hz) creeping in under Sub Tunnel, a heartbeat, a soft glass pad, a slow
          falling theremin-like lead through the Liminal Hz preset, toms and a heavy snare; tight impacts, and
          nothing left ringing over the drop.
          bars 0-3 intro, 4-5 swell, 6 IMPACT (title), 6-13 groove, 14-17 breakdown, 18 IMPACT (drop), 18-22, 23 end
  cyber   100 BPM in 4/4, D minor, 32 bars (77 s): slow, heavy cyberpunk. A broken kick (1, the and of 3), a
          backbeat snare, a pulsing 8th bass, warm 8th-gated chords, a soft square hook, glitches.
  trap    140 BPM in 4/4 (a half-time feel), D minor, 28 bars (53 s): modern trap. 808s that slide, a clap on 3,
          hats in 8ths and 16ths with triplet and 32nd rolls, a dark mono synth line (3-3-2, gliding) through Dream Ping-Pong,
          a theremin-like counter-line through the Liminal Hz preset, a stop before the drop.
          bars 0-3 boot, 4 IMPACT (title), 4-11 groove, 12-15 features, 16-19 breakdown and build, 20 IMPACT (drop),
          20-27 drop with the hook, 28 IMPACT (end)
Every note is on the grid, with fixed (not random) tuning. Each stem is a 44.1 kHz stereo float WAV, and every
stem is played through Liminal Hz (none is left dry); STEMS[style] says which factory preset and settings (tools/trailer/render.sh), LEVEL[style] how loud
it sits against the drums and GATES[style] where a stem's reverb is cut so it can't ring over a hit (mix.py)."""
import os, sys
import numpy as np
from scipy.signal import butter, sosfilt

STYLE = os.environ.get("TRAILER_STYLE") or (sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] in ("dark", "cyber", "trap") else "dark")
SR = 44100
BPM, BPB, BARS = {"dark": (120, 5, 26), "cyber": (100, 4, 32), "trap": (140, 4, 31)}[STYLE]
BEAT = 60 / BPM; BAR = BPB * BEAT
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

def braam(root=38, dur=2.2, level=1.0, bright=2600):
    """a low brass-like blast: detuned saw stacks, the filter opening and closing, driven"""
    n = int((dur + 0.5) * SR); t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for m in (root - 12, root, root + 7, root + 12):           # octaves and a fifth: one solid chord, not a cluster
        for ch in range(2):
            for det in ((-0.035, 0.035) if ch == 0 else (-0.025, 0.045)):   # a slight, fixed spread (stereo width)
                out[:, ch] += saw(hz(m) * 2 ** (det / 12), n, phase=0.0) * (1.4 if m < root else 1.0)
    cut = 180 + bright * np.exp(-t * 2.2) * (1 - np.exp(-t * 40))
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
def tri(f, n, phase=0.0):
    """a triangle wave: soft (odd harmonics falling as 1/n^2)"""
    return 2 * np.abs(saw(f, n, phase)) - 1

def soft_pulse(m, dur, cutoff=900, level=0.4):
    """one note of the dark ostinato: a sine with a triangle for its edge, a quiet octave above; a low-pass that
    opens a little on each note but never past ~700 Hz (round and sub-heavy, nothing harsh)"""
    n = int((dur + 0.08) * SR); t = np.arange(n) / SR; f = hz(m)
    x = np.sin(2 * np.pi * f * t) + 0.45 * tri(f, n) + 0.12 * np.sin(2 * np.pi * 2 * f * t + 0.6)
    y = np.zeros(n); seg = 512; top = min(cutoff, 700)
    for i in range(0, n, seg):
        fc = top * (1 + 0.8 * np.exp(-t[i] / 0.06))
        y[i:i + seg] = lp(x[i:i + seg], fc)
    return np.tanh(y * 1.1) * env(n, 0.008, 0.08, 0.8, dur, 0.06) * level

def ghost_lead(m, dur, level=0.2, cutoff=1800):
    """the dark lead: a sine and a little triangle with a slow vibrato that fades in (theremin-like), a slow swell"""
    n = int((dur + 0.4) * SR); t = np.arange(n) / SR; f = hz(m)
    vib = 1 + 0.009 * np.sin(2 * np.pi * 5.0 * t) * np.clip((t - 0.25) / 0.5, 0, 1)
    ph = np.cumsum(f * vib) / SR
    x = np.sin(2 * np.pi * ph) + 0.3 * (2 * np.abs(2 * (ph % 1.0) - 1) - 1)
    return lp(x, cutoff) * env(n, 0.25, 0.3, 0.9, dur, 0.35) * level

def strings(notes, dur, level=0.1, cutoff=1600):
    """a glass pad: triangles with a small fixed spread, slow in and out"""
    n = int((dur + 1.2) * SR); out = np.zeros((n, 2))
    for m in notes:
        for ch, det in ((0, -0.02), (1, 0.02)):
            out[:, ch] += tri(hz(m) * 2 ** (det / 12), n, 0.25 * ch) * 1.3
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
def silence_before(S, bars, keys, beats=0.5):
    """everything in keys stops beats before each of bars (the breath before a hit)"""
    for bar in bars:
        a, b = int((t_of(bar) - BEAT * beats) * SR), int(t_of(bar) * SR)
        for k in keys: S[k][a:b] = 0

def square_hook(m, dur, level=0.16):
    """the cyber hook: a square, short and exact"""
    n = int((dur + 0.08) * SR); t = np.arange(n) / SR
    x = np.sign(np.sin(2 * np.pi * hz(m) * t)) + 0.4 * np.sign(np.sin(2 * np.pi * hz(m) * 2 * t))
    return lp(x, 2200) * env(n, 0.01, 0.1, 0.7, dur, 0.06) * level

def gate_chord(notes, dur, level=0.06, cutoff=5000):
    """one step of a gated chord: plain saws (no detune), cut off sharp"""
    n = int((dur + 0.01) * SR); out = np.zeros(n)
    for m in notes: out += saw(hz(m), n, 0.0)
    return lp(hp(out, 150), cutoff) * env(n, 0.004, 0.06, 0.8, dur, 0.012) * level

def roll_bass(m, dur, level=0.42, cutoff=1100):
    """a rolling bass step: a saw and a sub, driven, short"""
    n = int((dur + 0.01) * SR); t = np.arange(n) / SR; f = hz(m)
    x = saw(f, n, 0.0) + 0.9 * np.sin(2 * np.pi * f * t)
    return np.tanh(lp(x, cutoff) * 2.5) * env(n, 0.002, 0.03, 0.8, dur, 0.01) * level

def boot_sweep(dur, level=0.3):
    """a machine starting up: a square sweeping up three octaves with a stepped (sample-and-hold) pitch"""
    n = int(dur * SR); t = np.arange(n) / SR
    steps = np.floor(t / 0.06) * 0.06
    f = 110 * 2 ** (3 * steps / dur)
    return lp(np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)), 3000) * (t / dur) * level

# ---- dark: 120 BPM, 5/4 (3 + 2) -----------------------------------------------------------------------------------
def build_dark():
    S = {k: stem() for k in ("drone", "hits", "beat", "swell", "drums", "pulse", "strings", "lead", "texperc", "sub")}
    ROOTS = [38, 38, 34, 33]                                      # D, D, Bb, A
    PADS = [[50, 53, 57], [50, 53, 57], [46, 50, 53], [45, 49, 52]]  # Dm, Dm, Bb, A
    OST = [0, 0, 12, 0, 0, 12, 0, 12, 0, 0]                        # the ostinato: 10 eighths, 3 + 2 accents
    t_end = t_of(23) + 4.5
    tt = np.arange(int(t_end * SR)) / SR                          # the drone: a sub on D, dark air, all the way
    drone = np.sin(2 * np.pi * hz(26) * tt) * 0.35 + lp(rng.standard_normal(len(tt)), 70) * 0.25
    drone *= np.clip(tt / 4.0, 0, 1) * np.clip((t_end - tt) / 3.0, 0, 1)
    place(S["drone"], drone, 0.0)
    for bar in range(0, 6):                                       # the heartbeat, on the 3 + 2 accents
        for b in (0, 3):
            if bar == 5 and b == 3: continue
            place(S["beat"], heartbeat(0.45 + 0.55 * (bar * 2 + b / 3) / 11), t_of(bar, b))
    for bar in range(0, 23):                                      # the ostinato, dry, creeping in, then locked
        if 14 <= bar < 16: continue
        root = ROOTS[(bar - 6) % 4] if bar >= 6 else 38
        cutoff = (260 + 120 * bar) if bar < 6 else (480 if 16 <= bar < 18 else 1000 if bar < 18 else 1500)
        lvl = (0.12 + 0.03 * bar) if bar < 6 else 0.34
        for k, o in enumerate(OST):
            if bar in (5, 17) and k >= 9: continue
            place(S["pulse"], soft_pulse(root - 12 + o, 0.5 * BEAT * 0.8, cutoff, lvl), t_of(bar, k * 0.5))
    for bar in (0, 2, 4, 14, 16):
        place(S["strings"], strings([50, 53, 57] if bar % 4 == 0 else [46, 50, 53], 2 * BAR - 0.3, 0.08, 800), t_of(bar))
    place(S["swell"], reverse_swell(2 * BAR - 0.4, 0.9), t_of(4))
    place(S["swell"], riser(2 * BAR - 0.4, 0.7), t_of(16))
    for bar in (6, 18, 23): place(S["hits"], impact(1.0, 0.0, 0.35), t_of(bar))
    for bar in list(range(6, 14)) + list(range(18, 23)):
        heavy = bar >= 18
        root, pad = ROOTS[(bar - 6) % 4], PADS[(bar - 6) % 4]
        if (bar - 6) % 2 == 0: place(S["strings"], strings(pad + [pad[0] + 12], 2 * BAR - 0.2, 0.08, 1300), t_of(bar))
        for b in ((0, 3) if not heavy else (0, 1.5, 3, 4)): place(S["drums"], kick(0.9 if heavy else 0.75), t_of(bar, b))
        for b in (2, 4.5) if not heavy else (2, 4.5): place(S["drums"], big_snare(1.0 if heavy else 0.85), t_of(bar, b))
        for k in range(10): place(S["drums"], hat(level=0.08 if k % 2 == 0 else 0.05), t_of(bar, k * 0.5), pan=0.25)
        if bar in (9, 13, 21):
            for k, m in enumerate((52, 48, 45, 41)): place(S["drums"], syn_tom(m), t_of(bar, 3 + k * 0.5), pan=0.5 - 0.33 * k)
        place(S["sub"], sub_note(root - 12 + 12, 3 * BEAT - 0.05, 0.5 if heavy else 0.4), t_of(bar))
        place(S["sub"], sub_note(root - 12 + 12, 2 * BEAT - 0.05, 0.45 if heavy else 0.36), t_of(bar, 3))
        if heavy: place(S["hits"], braam(root, 1.4, 0.6, 1100), t_of(bar))
    LINE = [(69, 0, 3), (67, 3, 2), (65, 0, 3), (64, 3, 2), (62, 0, 5), (61, 0, 5)]   # the lead: falling, slow
    for k, (m, b, d) in enumerate(LINE):
        bar = [8, 8, 9, 9, 10, 11][k]
        place(S["lead"], ghost_lead(m, d * BEAT - 0.1, 0.2), t_of(bar, b))
    for k, (m, b, d) in enumerate(LINE):
        place(S["lead"], ghost_lead(m + 12, d * BEAT - 0.1, 0.17, 2200), t_of([19, 19, 20, 20, 21, 22][k], b))
    place(S["hits"], braam(38, 3.5, 0.9, 1300), t_of(23))
    silence_before(S, (6, 18), ("drums", "texperc", "pulse", "sub", "lead", "strings", "beat"))
    for bar in range(14, 18):                                     # breakdown: drums through Tuned Chord
        for b in (0, 3): place(S["texperc"], kick(0.4) * 0.8, t_of(bar, b))
        for b in (2, 4.5): place(S["texperc"], snare(0.8), t_of(bar, b))
        for k in range(10): place(S["texperc"], hat(level=0.1), t_of(bar, k * 0.5), pan=0.2)
    for k in range(18):                                           # bar 17: a roll, 8ths then 16ths
        b = k * 0.5 if k < 6 else 3 + (k - 6) * 0.25 * (2 / 3)
        if b < 4.5: place(S["drums"], snare(0.6) * (0.3 + 0.7 * k / 18), t_of(17, b))
    return S

# ---- cyber: 128 BPM, 4/4 ------------------------------------------------------------------------------------------
def build_cyber():
    S = {k: stem() for k in ("drone", "hits", "swell", "drums", "bass", "chords", "hook", "fx", "texperc", "sub")}
    CH = [(38, [62, 65, 69]), (41, [65, 69, 72]), (36, [60, 64, 67]), (34, [58, 62, 65])]   # Dm F C Bb
    GATE = [1, 0, 1, 1, 0, 1, 1, 0]                               # the chord gate, 8ths
    HOOK = [(74, 0, 0.5), (72, 0.75, 0.25), (69, 1, 0.5), (72, 1.5, 0.5), (74, 2.5, 0.5), (77, 3, 0.75)]
    t_end = t_of(28) + 4.0
    tt = np.arange(int(t_end * SR)) / SR
    hum = lp(saw(hz(26), len(tt), 0.0) * 0.3 + lp(rng.standard_normal(len(tt)), 160) * 0.3, 350)
    place(S["drone"], hum * np.clip(tt / 2.0, 0, 1) * np.clip((t_end - tt) / 3.0, 0, 1), 0.0)
    # boot (0-3): the machine starting, data, the bass stepping in on beats
    place(S["fx"], boot_sweep(2 * BAR - 0.2, 0.28), t_of(0))
    pat = [1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0]
    for bar in range(0, 4):
        for k in range(16):
            if pat[k] and not (bar == 3 and k >= 12): place(S["fx"], blip([88, 93, 86, 98][k % 4], 0.12), t_of(bar, k * 0.25), pan=0.5 if k % 2 else -0.5)
    for bar in (2, 3):
        for b in range(4):
            if bar == 3 and b == 3: continue
            place(S["drums"], dist_kick(0.6 + 0.1 * b), t_of(bar, b))
    place(S["swell"], reverse_swell(BAR - 0.2, 0.9), t_of(3))
    for bar in (4, 20, 28): place(S["hits"], impact(1.0, 0.05, 0.6), t_of(bar))
    # the groove (4-15) and the drop (20-27)
    for bar in list(range(4, 16)) + list(range(20, 28)):
        drop = bar >= 20
        root, chord = CH[(bar - 4) % 4]
        for b in ((0, 2.5) if not drop else (0, 1.75, 2.5)): place(S["drums"], dist_kick(1.0 if drop else 0.9), t_of(bar, b))
        for b in (1, 3): place(S["drums"], big_snare(0.9 if drop else 0.8), t_of(bar, b))
        for k in range(8): place(S["drums"], hat(level=0.07 if k % 2 else 0.04), t_of(bar, k * 0.5), pan=0.3)
        for k in range(8):                                         # the bass: a slow 8th pulse, an octave up on the and of 4
            m = root - 12 + (12 if drop and k == 7 else 0)
            place(S["bass"], roll_bass(m, 0.5 * BEAT * 0.7, 0.4 if drop else 0.34, 800 if drop else 650), t_of(bar, k * 0.5))
        place(S["sub"], sub_note(root - 12, 4 * BEAT - 0.05, 0.4), t_of(bar))
        if bar >= 8 or drop:
            for k, g in enumerate(GATE):
                if g: place(S["chords"], gate_chord(chord + [chord[0] + 12], 0.5 * BEAT * 0.7, 0.06 if not drop else 0.07, 2600), t_of(bar, k * 0.5))
        if drop or 12 <= bar < 16:
            for m, b, d in HOOK: place(S["hook"], square_hook(m + (12 if bar >= 24 else 0), d * BEAT - 0.03), t_of(bar, b))
        if bar % 4 == 3: place(S["fx"], zap(0.16), t_of(bar, 3.5), pan=0.3)
        if bar in (11, 23): stutter(S, "drums", snare(0.8), t_of(bar, 3), 1.0, 0.125)
    # breakdown (16-19): no kick, the chords filtered down, drums through Tuned Chord, a build
    for bar in range(16, 20):
        root, chord = CH[(bar - 4) % 4]
        for k, g in enumerate(GATE):
            if g and not (bar == 19 and k >= 7): place(S["chords"], lp(gate_chord(chord, 0.5 * BEAT * 0.7, 0.07), 500 + 300 * (bar - 16)), t_of(bar, k * 0.5))
        if bar < 18:
            for b in (0, 2.5): place(S["texperc"], kick(0.4) * 0.8, t_of(bar, b))
            for b in (1, 3): place(S["texperc"], snare(0.8), t_of(bar, b))
    for bar in (18, 19):                                          # the build: snare 8ths, 16ths, 32nds
        step = 0.5 if bar == 18 else 0.25
        for k in range(int(4 / step)):
            b = k * step
            if bar == 19 and b >= 3.5: continue
            place(S["drums"], snare(0.5) * (0.4 + 0.5 * ((bar - 18) * 4 + b) / 8), t_of(bar, b))
        if bar == 19:
            for k in range(8): place(S["drums"], snare(0.6) * 0.85, t_of(19, 2.5 + k * 0.125))
    place(S["swell"], riser(2 * BAR - 0.3, 0.7), t_of(18))
    place(S["bass"], reese(26, 3.0, 0.45, 600), t_of(28))
    silence_before(S, (4, 20), ("drums", "texperc", "bass", "sub", "chords", "hook", "fx"))
    return S

# ---- trap ----------------------------------------------------------------------------------------------------------
def tr808(m, dur, level=0.5, from_m=None, glide=0.08):
    """an 808: a sine with a pitch punch (or a slide from from_m), saturated so it carries on small speakers"""
    n = int((dur + 0.06) * SR); t = np.arange(n) / SR
    if from_m is None: f = hz(m) * (1 + 0.9 * np.exp(-t * 45))
    else: f = hz(m) * 2 ** ((from_m - m) / 12 * np.clip(1 - t / glide, 0, 1) ** 2)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR)
    x = np.tanh(x * 2.4) / np.tanh(2.4) + hp(rng.standard_normal(n), 4000) * np.exp(-t * 500) * (0.12 if from_m is None else 0)
    return lp(x, 3000) * env(n, 0.002, 0.0, 1.0, dur, 0.05) * np.exp(-t * 0.5) * level

def clap(level=0.6):
    """a trap clap: three tight bursts and a short band-passed tail, layered on a snare's body"""
    n = int(0.4 * SR); t = np.arange(n) / SR; out = np.zeros(n)
    for d in (0.0, 0.009, 0.019, 0.03):
        i = int(d * SR); m = n - i
        out[i:] += bp(rng.standard_normal(m), 1000, 6000) * np.exp(-np.arange(m) / SR * 160) * 0.6
    out += bp(rng.standard_normal(n), 1100, 5000) * np.exp(-t * 16) * 0.35 * (t > 0.03)
    body = np.sin(2 * np.pi * 200 * t) * np.exp(-t * 25) * 0.4
    return np.tanh((out + body) * 1.6) * level

V808 = [(0, 0, 1.4, None), (1.5, 0, 1.1, None), (2.75, 0, 1.2, None)]          # (beat, +semitones, beats, slide from)
D808 = [(0, 0, 1.4, None), (1.5, 0, 0.9, None), (2.5, 12, 0.45, 0), (3.0, 0, 0.9, 12)]
def trap_808(bar):
    if 4 <= bar < 12: return V808
    if 12 <= bar < 18: return [(0, 0, 3.6, None)]
    if 20 <= bar < 28: return D808 if bar % 4 != 3 else D808[:2] + [(2.5, 7, 0.45, 0), (3.0, 12, 0.9, 7)]
    return []
def trap_hits():
    """the 808s' times and the claps' times (the edit cuts on them)"""
    return ([t_of(b, x[0]) for b in range(BARS) for x in trap_808(b)],
            [t_of(b, 2) for b in list(range(4, 18)) + list(range(20, 28))])

def blep(ph, dt):
    """the PolyBLEP correction at a phase wrap (band-limits a saw or square: no aliasing)"""
    y = np.zeros_like(ph)
    k = ph < dt; x = ph[k] / dt[k]; y[k] = x + x - x * x - 1
    k = ph > 1 - dt; x = (ph[k] - 1) / dt[k]; y[k] = x * x + x + x + 1
    return y

def dark_mono(m, dur, level=0.3, from_m=None, glide=0.045):
    """the trap lead: a dark mono synth. A band-limited saw and a square an octave down, a little sub, gliding from the
    last note; a low-pass that opens a little on each note and closes to ~600 Hz; soft drive"""
    n = int((dur + 0.12) * SR); t = np.arange(n) / SR
    semis = 0.0 if from_m is None else (from_m - m) * np.exp(-t / glide)
    f = hz(m) * 2 ** (semis / 12) * np.ones(n)
    dt = f / SR; ph = np.cumsum(dt) % 1.0
    sawv = 2 * ph - 1 - blep(ph, dt)
    dt2 = dt / 2; ph2 = np.cumsum(dt2) % 1.0
    sq = np.where(ph2 < 0.5, 1.0, -1.0) + blep(ph2, dt2) - blep((ph2 + 0.5) % 1.0, dt2)
    x = 0.55 * sawv + 0.35 * sq + 0.3 * np.sin(2 * np.pi * np.cumsum(f / 2) / SR)
    y = np.zeros(n); seg = 256
    for i in range(0, n, seg):
        fc = 600 + 1300 * np.exp(-t[i] / 0.09)
        y[i:i + seg] = lp(x[i:i + seg], fc)
    y = lp(y, 2400)                                               # (a second pole: dark, never fizzy)
    return np.tanh(y * 1.4) / np.tanh(1.4) * env(n, 0.004, 0.12, 0.75, dur, 0.05) * level

def build_trap():
    S = {k: stem() for k in ("drone", "hits", "swell", "drums", "bass", "bells", "lead", "pad", "texperc", "fx")}
    CH = [(26, [62, 65, 69]), (34, [58, 62, 65]), (31, [55, 58, 62]), (33, [57, 61, 64])]   # Dm Bb Gm A
    MEL = [[(81, 0), (77, .75), (74, 1.5), (81, 2), (77, 2.75), (76, 3.5)],              # the lead: 3-3-2, twice a bar
           [(77, 0), (74, .75), (70, 1.5), (77, 2), (74, 2.75), (72, 3.5)],
           [(74, 0), (70, .75), (67, 1.5), (74, 2), (72, 2.75), (70, 3.5)],
           [(73, 0), (76, .75), (69, 1.5), (76, 2), (73, 2.75), (81, 3.5)]]
    LINE = [[(81, 0, 4)], [(82, 0, 4)], [(79, 0, 2), (77, 2, 2)], [(76, 0, 2.5), (73, 2.5, 1.5)]]   # the counter-line
    t_end = t_of(28) + 4.5
    tt = np.arange(int(t_end * SR)) / SR
    air = lp(rng.standard_normal(len(tt)), 220) * 0.3 + np.sin(2 * np.pi * hz(38) * tt) * 0.15
    place(S["drone"], air * np.clip(tt / 3.0, 0, 1) * np.clip((t_end - tt) / 3.0, 0, 1), 0.0)
    prev = None
    for bar in range(0, 28):
        root, chord = CH[bar % 4]
        place(S["pad"], strings(chord, BAR - 0.05, 0.07), t_of(bar))
        if bar < 18 or bar >= 20:          # the lead (filtered open over the intro)
            notes = MEL[bar % 4]
            for j, (m, b) in enumerate(notes):                   # mono and legato: each note glides from the last
                nb = notes[j + 1][1] if j + 1 < len(notes) else 4.0
                x = dark_mono(m - 12, (nb - b) * BEAT * 0.92, 0.3, prev)
                prev = m - 12
                if bar < 4: x = lp(x, 500 + 600 * bar)
                place(S["bells"], x, t_of(bar, b))
        for b, o, d, fr in trap_808(bar):
            place(S["bass"], tr808(root + o, d * BEAT, 0.55, None if fr is None else root + fr), t_of(bar, b))
        drop = bar >= 20
        if 4 <= bar < 18 or drop:
            place(S["drums"], clap(0.9 if drop else 0.8), t_of(bar, 2))
            step = 0.25 if drop else 0.5
            for k in range(int(4 / step)):
                b = k * step
                if bar % 2 == 1 and b >= 3: continue              # (the roll's place)
                if 12 <= bar < 16 and k % 2: continue
                place(S["drums"], hat(level=0.06 if k % 2 == 0 else 0.045), t_of(bar, b), pan=0.25)
            if bar % 2 == 1:                                      # rolls: triplets, then 32nds rising
                rate = 1 / 6 if bar % 4 == 1 else 1 / 8
                for k in range(int(1 / rate)):
                    place(S["drums"], hat(level=0.035 + 0.035 * k * rate), t_of(bar, 3 + k * rate), pan=0.25)
            if drop and bar % 2 == 0: place(S["drums"], hat(True, 0.05), t_of(bar, 3.5), pan=-0.3)
            if drop and bar % 4 == 2: place(S["drums"], clap(0.35), t_of(bar, 3.75))
        if drop:
            for m, b, d in LINE[bar % 4]: place(S["lead"], ghost_lead(m, d * BEAT - 0.1, 0.18, 2000), t_of(bar, b))
        if 12 <= bar < 18:                                        # the features: a rim pattern through Tuned Chord
            for b in (0.75, 1.5, 3.25): place(S["texperc"], snare(0.6) * 0.6, t_of(bar, b))
    for bar in (18, 19):                                          # the build: claps in 8ths, 16ths, 32nds
        step = 0.5 if bar == 18 else 0.25
        for k in range(int(4 / step)):
            b = k * step
            if bar == 19 and b >= 2.5: continue
            place(S["drums"], clap(0.3 + 0.5 * ((bar - 18) * 4 + b) / 8), t_of(bar, b))
        if bar == 19:
            for k in range(8): place(S["drums"], clap(0.75), t_of(19, 2.5 + k * 0.125))
    place(S["fx"], riser(2 * BAR - 0.3, 0.6), t_of(18))
    place(S["swell"], reverse_swell(BAR - 0.2, 0.8), t_of(3))
    place(S["bass"], tr808(26, 1.6 * BEAT, 0.5, 38, 0.5), t_of(3, 2.4))   # the slide into the title
    for bar in (4, 20, 28): place(S["hits"], impact(1.0, 0.04, 0.5), t_of(bar))
    place(S["bass"], tr808(26, 2.5, 0.6), t_of(28))
    last = None
    for m, b, d in [(62, 0, 0.5), (65, 0.5, 0.5), (69, 1.0, 0.5), (74, 1.5, 2.5)]:
        place(S["bells"], dark_mono(m, d * BEAT if d < 1 else d, 0.28, last), t_of(28, b)); last = m
    silence_before(S, (4, 20), ("drums", "bass", "bells", "pad", "lead", "texperc", "fx"))
    silence_before(S, (28,), ("drums", "bass", "lead"), 0.25)
    return S

def dist_kick(level=1.0):
    return np.tanh(kick(1.0) * 2.4) * 0.92 * level

# the plugin settings per stem (render.sh): factory preset number and key=value, as tools/demos/demo_host takes them
STEMS = {
    "dark": {
        "drone":   "10 mix=60 long_ir=1 feedback=40 decay=180 width=180",      # Glass Void, 20 s
        "hits":    "4 mix=24 decay=100 width=160",                             # Backrooms, short: impacts, braams
        "beat":    "12 mix=10",                                                # Sub Tunnel, barely: a close heartbeat
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "1 mix=18 duck=60",                                         # Empty Mall, ducked
        "pulse":   "12 mix=22 width=140",                                      # Sub Tunnel: the ostinato underground
        "strings": "10 mix=55 decay=150",                                      # Glass Void: a far, cold pad
        "lead":    "14 mix=50 decay=140",                                      # Liminal Hz: the eerie lead
        "texperc": "17 mix=75",                                                # Tuned Chord: drums that play a chord
        "sub":     "12 mix=8",                                                 # Sub Tunnel, a touch: it stays tight
    },
    "cyber": {
        "drone":   "12 mix=50",                                                # Sub Tunnel: the hum
        "hits":    "10 mix=30 decay=110",                                      # Glass Void, short
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "2 mix=10 duck=30",                                         # Pool Rooms: tight and bright
        "bass":    "12 mix=14",                                                # Sub Tunnel, a little: still tight
        "chords":  "3 mix=30 duck=70 width=180",                               # Fluorescent Hall, ducked under the kick
        "hook":    "9 mix=30",                                                 # Intercom Echo: a lo-fi edge
        "fx":      "7 mix=45",                                                 # Dream Ping-Pong
        "texperc": "17 mix=75",                                                # Tuned Chord
        "sub":     "12 mix=8",                                                 # Sub Tunnel, a touch
    },
    "trap": {
        "drone":   "10 mix=55",                                                # Glass Void: the air
        "hits":    "4 mix=20 decay=90 width=160",                              # Backrooms, short
        "swell":   "11 mix=70",                                                # Reverse Bloom
        "drums":   "2 mix=10 duck=30",                                         # Pool Rooms: clap and hats, tight
        "bass":    "12 mix=6",                                                 # Sub Tunnel, a touch: the 808s
        "bells":   "7 mix=35 duck=50",                                         # Dream Ping-Pong, ducked: the mono synth
        "lead":    "14 mix=50 decay=140",                                      # Liminal Hz: the counter-line
        "pad":     "10 mix=60 decay=150",                                      # Glass Void: a far pad
        "texperc": "17 mix=75",                                                # Tuned Chord
        "fx":      "7 mix=40",                                                 # Dream Ping-Pong
    },
}
LEVEL = {   # dB against the drums, by loudness while each plays (mix.py)
    "dark": {"drums": 0.0, "hits": 0.0, "beat": -1.0, "pulse": -2.0, "sub": -5.0, "texperc": -2.0, "swell": -5.0,
             "strings": -9.0, "lead": -7.0, "drone": -9.0},
    "cyber": {"drums": 0.0, "hits": -1.0, "bass": -3.0, "sub": -7.0, "chords": -6.0, "hook": -6.0, "fx": -12.0,
              "texperc": -2.0, "swell": -5.0, "drone": -12.0},
    "trap": {"drums": 0.0, "hits": -1.0, "bass": 1.0, "bells": -5.0, "lead": -9.0, "pad": -13.0, "drone": -15.0,
             "texperc": -4.0, "swell": -6.0, "fx": -10.0},
}
# where a stem's sound (reverb tails included) is cut, so nothing rings over a hit: (stem, bar)
# (stems with nothing of their own to play for the two bars after that bar: their tails are cut there)
GATES = {"dark": [("texperc", 18), ("swell", 18), ("swell", 6)],
         "cyber": [("texperc", 20), ("swell", 20), ("swell", 4), ("fx", 4)],
         "trap": [("texperc", 20), ("swell", 20), ("swell", 4), ("fx", 20)]}
BUILD = {"dark": build_dark, "cyber": build_cyber, "trap": build_trap}

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
