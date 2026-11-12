#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""gen_audio.py <out dir> -- the dry phrases for the three demos, synthesized (no samples): 44.1 kHz stereo float WAVs.
  pad.wav     two soft, detuned-saw pad chords (A minor add9, F major 7), for the Backrooms demo
  drums.wav   two bars of a synthesized kit at 92 bpm, for the Empty Mall demo
  hits.wav    two bars of sparse kick and snare at 80 bpm, for the reverse-swell demo
  keys.wav    a two-bar electric-piano arpeggio at 100 bpm, for the Dream Ping-Pong demo
  stabs.wav   three short synth chord stabs, for the Liminal Hz preset demo
  perc.wav    two bars of a busier kit at 100 bpm (Struck Pipe, Tuned Chord, SYNC)
  vox.wav     a sung "aah" line (a saw through vowel formants), for Grain Cloud
  hats.wav    two bars of 16th hi-hats at 110 bpm, for Breath
  snaps.wav   one snare three times, for Bouncing Ball
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

def epiano(m, n, vel=1.0):                    # a soft FM electric-piano note (2:1 bell over a sine), plucked
    t = np.arange(n) / SR; f = note_hz(m)
    mod = np.sin(2 * np.pi * f * 2 * t) * 1.6 * np.exp(-t * 9)
    v = np.sin(2 * np.pi * f * t + mod) * np.exp(-t * 2.6) * (1 - np.exp(-t * 400))
    return v * vel

def keys(total_s, bpm=100):                   # a two-bar arpeggio in D minor (8ths), for the ping-pong echoes
    out = np.zeros((int(total_s * SR), 2)); e8 = 30 / bpm
    seq = [62, 65, 69, 72, 69, 65, 62, 65, 60, 64, 67, 72, 67, 64, 60, 57]   # Dm, then C ending low
    for k, m in enumerate(seq):
        x = epiano(m, int(1.2 * SR), 0.9 if k % 4 == 0 else 0.7)
        place(out, x * 0.5, 0.15 + k * e8, pan=0.0)
    return out

def stabs(total_s):                           # three short chord stabs (Am9, Fmaj7, Em7), the room left to the tail
    out = np.zeros((int(total_s * SR), 2))
    for notes, t0 in (([57, 60, 64, 67, 71], 0.2), ([53, 57, 60, 64, 69], 1.4), ([52, 55, 59, 62, 67], 2.6)):
        n = int(0.6 * SR); e = env_adsr(n, 0.004, 0.08, 0.6, 0.16, 0.05)
        for m in notes:
            for side, det in ((0, -0.08), (1, 0.08)):
                v = lp(saw(note_hz(m) * 2 ** (det / 12), n, rng.random()) * e * 0.08, 2600, 2)
                i0 = int(t0 * SR); out[i0:i0 + n, side] += v
    return out

def rim(n=int(0.12 * SR)):                    # a rimshot: a high knock and a click
    t = np.arange(n) / SR
    return (np.sin(2 * np.pi * 1700 * t) * 0.5 + np.sin(2 * np.pi * 820 * t) * 0.4) * np.exp(-t * 45) + \
        0.4 * hp(rng.standard_normal(n), 2000) * np.exp(-t * 120)

def perc(total_s, bpm=100):                   # two bars of a busier kit (kick, snare, rim, 16th hats) at 100 BPM
    out = np.zeros((int(total_s * SR), 2)); beat = 60 / bpm
    for bar in range(2):
        b0 = 0.15 + bar * 4 * beat
        for k in (0, 1.75, 2.5): place(out, kick(), b0 + k * beat)
        for k in (1, 3): place(out, snare(), b0 + k * beat)
        for k in (0.75, 3.5): place(out, rim() * 0.5, b0 + k * beat, pan=-0.3)
        for k in range(16): place(out, hat(level=0.13 if k % 4 == 2 else 0.08), b0 + k * beat / 4, pan=0.3)
    return out

def formant(x, freqs=((700, 110), (1220, 120), (2600, 180)), gains=(1.0, 0.6, 0.25)):
    from scipy.signal import lfilter
    y = np.zeros_like(x)
    for (fc, bw), g in zip(freqs, gains):     # two-pole resonators (an "aah")
        r = np.exp(-np.pi * bw / SR); th = 2 * np.pi * fc / SR
        y += g * lfilter([1 - r], [1, -2 * r * np.cos(th), r * r], x)
    return y

def vox(total_s):                             # a sung "aah" line (a saw through vowel formants, with vibrato)
    out = np.zeros((int(total_s * SR), 2))
    for m, t0, dur in ((64, 0.2, 0.9), (67, 1.15, 0.6), (69, 1.8, 0.6), (67, 2.45, 0.5), (64, 3.0, 1.6)):
        n = int((dur + 0.3) * SR); t = np.arange(n) / SR
        f = note_hz(m) * (1 + 0.006 * np.sin(2 * np.pi * 5.2 * t) * np.minimum(1, t / 0.4))
        ph = np.cumsum(f) / SR
        src = (2 * (ph % 1.0) - 1) + 0.15 * rng.standard_normal(n)
        v = formant(src) * env_adsr(n, 0.06, 0.2, 0.8, dur, 0.15) * 0.25
        place(out, v, t0)
    return out

def hats(total_s, bpm=110):                   # two bars of 16th hi-hats with an open hat and a shaker feel
    out = np.zeros((int(total_s * SR), 2)); beat = 60 / bpm
    for bar in range(2):
        b0 = 0.15 + bar * 4 * beat
        for k in range(16):
            lv = (0.16, 0.07, 0.11, 0.06)[k % 4]
            place(out, hat(level=lv), b0 + k * beat / 4, pan=0.2 if k % 2 else -0.1)
        for k in (1.5, 3.5): place(out, hat(n=int(0.35 * SR), level=0.14), b0 + k * beat, pan=0.1)
    return out

def snaps(total_s):                           # one snare, three times, with room around it
    out = np.zeros((int(total_s * SR), 2))
    for t0 in (0.2, 1.4, 2.6): place(out, snare() * 1.1, t0)
    return out

# ---- sound design (1.4): sources for the experimental demos, with their own generator so the ones above don't change
sd = np.random.default_rng(1404)

def whisper(total_s):                         # four whispered syllables: noise through moving vowel formants
    out = np.zeros((int(total_s * SR), 2))
    vowels = [((300, 90), (2300, 160), (3000, 200)), ((700, 110), (1100, 120), (2500, 180)),
              ((450, 100), (800, 110), (2600, 180)), ((550, 100), (1800, 140), (2600, 180))]
    for k, (t0, dur) in enumerate(((0.15, 0.45), (0.75, 0.35), (1.25, 0.55), (2.05, 0.8))):
        n = int((dur + 0.15) * SR)
        v = formant(sd.standard_normal(n), vowels[k], (1.0, 0.8, 0.5)) * env_adsr(n, 0.05, 0.1, 0.8, dur, 0.08)
        v += hp(sd.standard_normal(n), 5000) * 0.05 * env_adsr(n, 0.005, 0.04, 0.0, 0.05, 0.02)   # the "s"/"t" edge
        place(out, v * 0.6, t0, pan=0.05)
    return out

def drips(total_s):                           # water drops: tiny sine chirps rising fast, at uneven times and pitches
    out = np.zeros((int(total_s * SR), 2))
    for t0, f0, pan in ((0.1, 700, -0.3), (0.55, 950, 0.2), (0.8, 620, 0.4), (1.35, 1100, -0.1), (1.6, 840, -0.4),
                        (2.2, 760, 0.3), (2.45, 1250, 0.0), (2.9, 690, -0.2)):
        n = int(0.09 * SR); t = np.arange(n) / SR
        f = f0 * (1 + 1.6 * (1 - np.exp(-t / 0.012)))
        d = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.018) * np.minimum(1, t / 0.0015)
        place(out, d * 0.8, t0, pan)
    return out

def wind(total_s):                            # two gusts of wind: noise, a band-pass sweeping slowly, swelling and fading
    n = int(total_s * SR); out = np.zeros((n, 2))
    for ch in (0, 1):
        x = sd.standard_normal(n); y = np.zeros(n); seg = 2048; t = np.arange(n) / SR
        for i in range(0, n, seg):
            fc = 500 + 900 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.35 * t[i] + ch))
            y[i:i + seg] = lp(hp(x[i:i + seg], fc * 0.5), fc * 1.6)
        g = np.clip(np.sin(np.pi * np.clip(t / 2.2, 0, 1)) ** 2 + 0.8 * np.sin(np.pi * np.clip((t - 1.8) / 2.0, 0, 1)) ** 2, 0, 1)
        out[:, ch] = y * g * (t < 3.9)
    return out

def ticks(total_s):                           # five glitch clicks: tiny filtered impulses, unevenly spaced
    out = np.zeros((int(total_s * SR), 2))
    for t0, fc, pan in ((0.1, 3000, -0.4), (0.42, 1800, 0.3), (0.95, 4200, 0.0), (1.2, 2400, 0.5), (1.9, 3300, -0.2)):
        n = int(0.02 * SR); t = np.arange(n) / SR
        c = np.sin(2 * np.pi * fc * t) * np.exp(-t / 0.0025) + 0.3 * sd.standard_normal(n) * np.exp(-t / 0.001)
        place(out, c, t0, pan)
    return out

def riser(total_s):                           # a 3 s riser: noise opening up and a sine sweeping up under it, cut dead
    n = int(total_s * SR); out = np.zeros((n, 2)); m = int(3.0 * SR); t = np.arange(m) / SR
    for ch in (0, 1):
        x = sd.standard_normal(m); y = np.zeros(m); seg = 2048
        for i in range(0, m, seg): y[i:i + seg] = lp(x[i:i + seg], 300 + 9000 * (t[i] / 3.0) ** 2)
        sw = np.sin(2 * np.pi * np.cumsum(110 * 2 ** (3 * t / 3.0)) / SR) * 0.5
        out[:m, ch] = (y * 0.8 + sw) * (t / 3.0) ** 1.5
    return out

def hum(total_s):                             # a fluorescent tube: a 60 Hz buzz with its harmonics, flickering, a tick or two
    n = int(total_s * SR); out = np.zeros((n, 2)); t = np.arange(n) / SR
    buzz = sum(np.sin(2 * np.pi * 60 * k * t + k) / k ** 1.2 for k in range(1, 40) if 60 * k < 8000) * 0.25
    buzz += 0.04 * np.sin(2 * np.pi * 120 * t) * sd.standard_normal(n)
    on = np.ones(n)
    for a, b in ((0.9, 0.97), (1.02, 1.08), (2.3, 2.36), (2.4, 2.75)): on[int(a * SR):int(b * SR)] = 0.12
    on = lp(on, 80) * (t < 3.6) * np.minimum(1, t / 0.05)
    for tk in (0.9, 2.3, 2.4): place(out, sd.standard_normal(int(0.004 * SR)) * 0.5, tk)
    out[:, 0] += buzz * on; out[:, 1] += buzz * on
    return out

def norm(x, peak=0.7):
    m = np.max(np.abs(x)); return x * (peak / m) if m > 0 else x

if __name__ == "__main__":
    d = sys.argv[1]; os.makedirs(d, exist_ok=True)
    write(os.path.join(d, "pad.wav"), norm(pad(13.0)))        # 5 s of chords, then room for the tail
    write(os.path.join(d, "drums.wav"), norm(drums(9.5)))     # 2 bars (5.2 s), then the tail
    write(os.path.join(d, "hits.wav"), norm(hits(9.5)))       # 2 bars (6.0 s), then the tail
    write(os.path.join(d, "keys.wav"), norm(keys(10.0)))      # 2 bars (4.8 s), then the echoes
    write(os.path.join(d, "stabs.wav"), norm(stabs(11.0)))    # 3 stabs (3.2 s), then the long tail
    write(os.path.join(d, "perc.wav"), norm(perc(9.0)))       # 2 bars at 100 BPM (4.8 s), then the tail
    write(os.path.join(d, "vox.wav"), norm(vox(9.0)))         # a 4.6 s line, then the tail
    write(os.path.join(d, "hats.wav"), norm(hats(8.0)))       # 2 bars at 110 BPM (4.4 s), then the tail
    write(os.path.join(d, "snaps.wav"), norm(snaps(7.0)))     # 3 snares (2.6 s), then the tail
    # sound design (1.4)
    write(os.path.join(d, "whisper.wav"), norm(whisper(9.0))) # 4 syllables (2.9 s), then the tail
    write(os.path.join(d, "drips.wav"), norm(drips(8.0)))     # 8 drops (3.0 s), then the tail
    write(os.path.join(d, "wind.wav"), norm(wind(10.0)))      # two gusts (3.9 s), then the tail
    write(os.path.join(d, "ticks.wav"), norm(ticks(8.0)))     # 5 clicks (2.0 s), then the tail
    write(os.path.join(d, "riser.wav"), norm(riser(10.0)))    # a 3 s riser, then the tail
    write(os.path.join(d, "hum.wav"), norm(hum(10.0)))        # a flickering tube (3.6 s), then the tail
    print("wrote", d)
