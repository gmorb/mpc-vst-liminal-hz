#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_feature_images.py [social dir] -- images that explain 1.4's features, from the plugin's real output and page:
  docs/images/feature-duck.png, -sync.png, -textures.png, -presets.png   (1280x640, the README)
  and, given a folder, the same four as 1080x1080 social posts there.
Needs tools/demos/render_audio.sh's renders (vst/build/demos) and, for Duck, two wet-only renders:
  demo_host <so> drums.wav fduck0_wet.wav fduck0.json 4 mix=100 decay=200 duck=0   (and duck=80 -> fduck80)
Duck's curves are the plugin's real output; SYNC's echo times are what it computes (1/8 D at 100 BPM = 450 ms); the
Textures' shapes are the factory IRs themselves."""
import json, os, sys, wave
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFont
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "demos"))
import render_page
FONTS = render_page.FONTS
DEMOS = os.path.join(ROOT, "vst", "build", "demos")
OUT = os.path.join(ROOT, "docs", "images")
IV, GR, DIM, AMB, LINE = (236, 230, 218), (160, 154, 144), (100, 96, 90), (222, 186, 120), (58, 55, 51)
BG = (12, 12, 14)

def F(style, size): return ImageFont.truetype(FONTS + {"r": "TitilliumWeb-Regular.ttf", "s": "TitilliumWeb-SemiBold.ttf",
                                                         "b": "TitilliumWeb-Bold.ttf"}[style], int(size))

def canvas(W, H):
    im = Image.new("RGB", (W, H), BG)
    tex = Image.open(os.path.join(ROOT, "vst", "art", "texture.png")).convert("RGB")
    s = max(W / tex.width, H / tex.height); tex = tex.resize((int(tex.width * s) + 1, int(tex.height * s) + 1))
    return Image.blend(im, tex.crop((0, 0, W, H)), 0.45)

def wordmark(d, x, y, size):
    f, fh = F("r", size), F("s", size)
    for ch in "LIMINAL":
        d.text((x, y), ch, font=f, fill=(214, 206, 192), anchor="ls"); x += d.textlength(ch, font=f) + size * 0.22
    d.text((x + size * 0.08, y), "Hz", font=fh, fill=IV, anchor="ls")

def wrap(d, text, font, width):
    out, line = [], ""
    for w in text.split():
        t = (line + " " + w).strip()
        if d.textlength(t, font=font) <= width: line = t
        else: out.append(line); line = w
    return out + [line]

def header(d, x, y, kicker, title, body, width, size=1.0):
    d.text((x, y), kicker, font=F("s", 22 * size), fill=AMB, anchor="ls")
    ts = 58 * size
    while d.textlength(title, font=F("s", ts)) > width and ts > 30: ts -= 2
    d.text((x, y + 62 * size), title, font=F("s", ts), fill=IV, anchor="ls")
    yy = y + 112 * size
    for para in body:
        for ln in wrap(d, para, F("r", 25 * size), width):
            d.text((x, yy), ln, font=F("r", 25 * size), fill=GR, anchor="ls"); yy += 36 * size
        yy += 14 * size
    return yy

def page_crop(state, box, scale, glow=9, options=None):
    st = json.load(open(os.path.join(DEMOS, state + ".json")))
    idx = {p["name"]: p["i"] for p in st["params"]}
    st["options"] = {idx["Playhead"]: glow, **(options or {})}
    im = render_page.render(st).crop(box)
    return im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)

def framed(base, im, xy):
    d = ImageDraw.Draw(base); x, y = xy
    d.rounded_rectangle((x - 4, y - 4, x + im.width + 3, y + im.height + 3), 10, outline=LINE, width=2)
    base.paste(im, (x, y))

def read_wav(p):
    with open(p, "rb") as f: data = f.read()
    pos = 12
    while pos + 8 <= len(data):
        tag, sz = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 8], "little")
        if tag == b"data": return np.frombuffer(data[pos + 8:pos + 8 + sz], dtype="<f4").reshape(-1, 2)
        pos += 8 + sz + (sz & 1)

def read_pcm(p):
    w = wave.open(p); n = w.getnframes(); b = w.readframes(n); sw = w.getsampwidth(); ch = w.getnchannels()
    if sw == 3:
        a = np.frombuffer(b, dtype=np.uint8).reshape(-1, 3)
        v = (a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8) | (a[:, 2].astype(np.int32) << 16)) << 8 >> 8
        return (v / 8388608.0).reshape(-1, ch)
    return np.frombuffer(b, dtype="<i2").reshape(-1, ch) / 32768.0

def env_db(x, hop=441):
    m = np.abs(x).max(axis=1); n = len(m) // hop
    r = np.sqrt(np.mean((m[: n * hop].reshape(n, hop)) ** 2, axis=1))
    return 20 * np.log10(r + 1e-6)

# ---- the four ----------------------------------------------------------------------------------------------------
def duck_plot(W, H):
    dry = read_wav(os.path.join(DEMOS, "drums.wav")); w0 = read_wav(os.path.join(DEMOS, "fduck0_wet.wav"))
    w8 = read_wav(os.path.join(DEMOS, "fduck80_wet.wav"))
    n = int(7.5 * 44100); dry, w0, w8 = dry[:n], w0[:n], w8[:n]
    e_dry, e0, e8 = env_db(dry, 1323), env_db(w0, 1323), env_db(w8, 1323)   # (30 ms windows)
    ref = max(e_dry.max(), e0.max())
    im = Image.new("RGB", (W, H), (16, 16, 18)); d = ImageDraw.Draw(im)
    lo = -48
    def y(v): return H - 30 - (max(lo, v - ref) - lo) / -lo * (H - 60)
    def xs(k, m): return 20 + (W - 40) * k / m
    for db in (0, -12, -24, -36):
        d.line((20, y(ref + db), W - 20, y(ref + db)), fill=(34, 33, 31))
    for k, v in enumerate(e_dry):                        # the dry drums: a strip of hits along the bottom
        if v - ref > -20: d.line((xs(k, len(e_dry)), H - 24, xs(k, len(e_dry)), H - 10), fill=(200, 194, 182), width=2)
    dots = []
    pts0 = [(xs(k, len(e0)), y(v)) for k, v in enumerate(e0)]
    d.polygon(pts0 + [(xs(len(e0) - 1, len(e0)), H - 30), (20, H - 30)], fill=(70, 58, 40))   # Duck 0: the area
    d.line(pts0, fill=(150, 126, 86), width=2, joint="curve")
    d.line([(xs(k, len(e8)), y(v)) for k, v in enumerate(e8)], fill=AMB, width=4, joint="curve")
    f = F("r", 20)
    d.text((W - 24, 26), "the drums (hits, below)", font=f, fill=(200, 194, 182), anchor="ra")
    d.text((W - 24, 52), "reverb, Duck 0%", font=f, fill=(150, 130, 98), anchor="ra")
    d.text((W - 24, 78), "reverb, Duck 80%", font=F("s", 20), fill=AMB, anchor="ra")
    return im

def make_duck(W, H, social=False):
    im = canvas(W, H); d = ImageDraw.Draw(im)
    body = ["Turns the reverb down while you play, and lets it bloom in the gaps: big rooms on snares and vocals "
            "without muddying the groove.",
            "Up to 24 dB at 100%, back within a quarter of a second. It only turns down what you hear: Feedback's tails "
            "carry on underneath. 0% is exactly as before."]
    if not social:
        header(d, 60, 90, "NEW IN 1.4  ·  OUT GROUP", "Duck", body, 470)
        framed(im, duck_plot(640, 330), (580, 70))
        framed(im, page_crop("duck80", (640, 420, 1040, 628), 0.86), (580, 440))
        d.text((920, 470), "Backrooms, Mix 100%:", font=F("r", 18), fill=DIM, anchor="ls")
        d.text((920, 494), "the plugin's real output", font=F("r", 18), fill=DIM, anchor="ls")
    else:
        header(d, 70, 110, "NEW IN 1.4", "Duck", body[:1], 940, 1.25)
        framed(im, duck_plot(940, 380), (70, 400))
        framed(im, page_crop("duck80", (640, 420, 1040, 628), 1.2), (70, 820))
        wordmark(d, 640, 1010, 34)
    return im

def make_sync(W, H, social=False):
    im = canvas(W, H); d = ImageDraw.Draw(im)
    body = ["Tap SYNC beside Pre-delay and the knob steps through note values at your project's tempo: 1/128 to 1/4, "
            "dotted and triplet. Change the tempo and it follows.",
            "Here: a snare at 100 BPM through Parking Slap, Pre-delay 1/8 D. The slap lands 450 ms later, on the grid."]
    gw, gh = (640, 320) if not social else (940, 360)
    g = Image.new("RGB", (gw, gh), (16, 16, 18)); gd = ImageDraw.Draw(g)
    beats = 8; bx = lambda b: 30 + (gw - 60) * b / beats
    for k in range(beats * 4 + 1):                         # the grid: 16ths, beats, bars
        x = bx(k / 4); strong = k % 16 == 0; beat = k % 4 == 0
        gd.line((x, 40, x, gh - 80), fill=(70, 66, 60) if strong else (44, 42, 39) if beat else (28, 27, 25), width=2 if strong else 1)
    for b in range(beats): gd.text((bx(b) + 4, gh - 76), str(b % 4 + 1), font=F("r", 18), fill=DIM)
    for b in (1, 3, 5, 7):                                # snares and their synced slaps (1/8 D = 0.75 beat)
        gd.rectangle((bx(b) - 5, 80, bx(b) + 5, 170), fill=IV)
        x2 = bx(b + 0.75); gd.rectangle((x2 - 5, 120, x2 + 5, 170), fill=AMB)
        gd.line((bx(b) + 8, 100, x2 - 8, 130), fill=(120, 100, 70), width=2)
    gd.text((30, 14), "snare", font=F("s", 20), fill=IV); gd.text((110, 14), "the reverb's start, 1/8 D later", font=F("s", 20), fill=AMB)
    notes = "0 · 1/128 · 1/64 · 1/32 · 1/16 T · 1/16 · 1/16 D · 1/8 T · 1/8 · 1/8 D · 1/4"
    gd.text((gw / 2, gh - 12), notes, font=F("r", 18 if not social else 22), fill=GR, anchor="ms")
    if not social:
        header(d, 60, 90, "NEW IN 1.4  ·  BESIDE PRE-DELAY", "Tempo SYNC", body, 470)
        framed(im, g, (580, 70))
        framed(im, page_crop("sync_on", (60, 420, 470, 628), 0.86), (580, 420))
    else:
        header(d, 70, 110, "NEW IN 1.4", "Tempo SYNC", body[:1], 940, 1.25)
        framed(im, g, (70, 420))
        framed(im, page_crop("sync_on", (60, 420, 470, 628), 1.25), (70, 800))
        wordmark(d, 640, 1010, 34)
    return im

TEXTURES = [("Struck Pipe", "Struck_Pipe", "a metal pipe, struck", "drums, percussion"),
            ("Grain Cloud", "Grain_Cloud", "hundreds of tiny grains", "vocals, pads"),
            ("Tuned Chord", "Tuned_Chord", "a chord of resonances", "drums, vocals"),
            ("Breath", "Breath", "a breath through vowels", "vocals, hi-hats"),
            ("Bouncing Ball", "Bouncing_Ball", "bounces, closer and closer", "snares, one-shots")]

def tex_card(name, fn, what, best, w, h):
    x = read_pcm(os.path.join(ROOT, "packaging", "factory", "Textures", fn + ".wav"))[:, 0]
    im = Image.new("RGB", (w, h), (16, 16, 18)); d = ImageDraw.Draw(im)
    wh = int(h * 0.42); top = 70
    n = w - 30; m = np.abs(x); edges = np.linspace(0, len(m), n + 1).astype(int)
    pk = np.array([m[a:b].max() if b > a else 0 for a, b in zip(edges[:-1], edges[1:])]); pk /= pk.max()
    for i, v in enumerate(pk):
        hh = max(1, (v ** 0.6) * wh / 2)
        d.line((15 + i, top + wh / 2 - hh, 15 + i, top + wh / 2 + hh), fill=AMB if i < n * 0.6 else (150, 126, 86))
    d.text((15, 40), name, font=F("s", 26), fill=IV, anchor="ls")
    fs = 19 if w >= 280 else 16
    d.text((15, top + wh + 34), what, font=F("r", fs), fill=GR, anchor="ls")
    d.text((15, top + wh + 60), "best on " + best, font=F("s", fs), fill=AMB, anchor="ls")
    return im

def make_textures(W, H, social=False):
    im = canvas(W, H); d = ImageDraw.Draw(im)
    body = ["Five factory IRs that aren't rooms at all: your sound played through an object, a cloud, a chord, a breath, "
            "a bounce. Each has a preset, in the PRESET list's fourth column."]
    if not social:
        header(d, 60, 90, "NEW IN 1.4  ·  FACTORY IRs", "Textures", body + ["Any .wav works the same way: try a drum hit or a vocal from your MPC as the IR."], 470)
        cw, ch = 220, 240
        for k, t in enumerate(TEXTURES):
            x = 580 + (k % 3) * (cw + 14); y = 70 + (k // 3) * (ch + 14)
            framed(im, tex_card(*t, cw, ch), (x, y))
        d.text((580 + 2 * (cw + 14) + cw / 2, 70 + ch + 14 + ch / 2), "+ any .wav\nof your own", font=F("s", 26), fill=GR, anchor="mm", align="center")
    else:
        header(d, 70, 110, "NEW IN 1.4", "Textures", body, 940, 1.25)
        cw, ch = 300, 250
        for k, t in enumerate(TEXTURES):
            x = 70 + (k % 3) * (cw + 20); y = 420 + (k // 3) * (ch + 20)
            framed(im, tex_card(*t, cw, ch), (x, y))
        d.text((70 + 2 * (cw + 20) + cw / 2, 420 + ch + 20 + ch / 2), "+ any .wav\nof your own", font=F("s", 30), fill=GR, anchor="mm", align="center")
        wordmark(d, 640, 1010, 34)
    return im

PROGRAMS = ["Init", "Drum: Empty Mall", "Drum: Pool Rooms", "Drum: Stairwell Flutter", "Drum: Parking Slap",
            "Drum: Struck Pipe", "Drum: Bouncing Ball", "Vocal: Tape Corridor", "Vocal: Intercom Echo", "Vocal: Glass Void",
            "Keys: Fluorescent Hall", "Keys: Dream Ping-Pong", "Keys: Liminal Hz", "Pad: Backrooms", "Pad: Shimmer Fog",
            "FX: Reverse Bloom", "FX: Sub Tunnel", "FX: Grain Cloud", "FX: Tuned Chord", "FX: Breath",
            "My IR: Big Hall", "My IR: Spring Box"]      # (the last two: your own IRs, examples)

def program_list(w, h, rows, start=0, hi=14):
    im = Image.new("RGB", (w, h), (16, 16, 18)); d = ImageDraw.Draw(im)
    rh = h / rows
    for k in range(rows):
        i = start + k
        if i >= len(PROGRAMS): break
        y = k * rh
        if i == hi: d.rectangle((0, y, w, y + rh), fill=(44, 38, 28))
        mine = PROGRAMS[i].startswith("My IR")
        d.text((18, y + rh / 2), PROGRAMS[i], font=F("s" if i == hi else "r", min(26, rh * 0.55)),
               fill=AMB if i == hi else (GR if mine else IV), anchor="lm")
    return im

def make_presets(W, H, social=False):
    im = canvas(W, H); d = ImageDraw.Draw(im)
    body = ["MPC's own preset menu at the top of the screen now lists every factory preset, grouped by what it's for, "
            "then your own IRs, which it picks up each time the plugin loads.",
            "And each factory IR says what it suits, under its name: \"Strange · best on chords, pads\"."]
    if not social:
        header(d, 60, 90, "NEW IN 1.4  ·  PRESETS", "MPC's preset menu", body, 470)
        framed(im, program_list(300, 520, 22, 0, 12), (580, 60))
        framed(im, page_crop("stabs", (0, 120, 430, 300), 0.78), (900, 60))
        d.text((900, 230), "the IR, its category and what it suits", font=F("r", 18), fill=DIM, anchor="ls")
        best = [("Drum \u00b7 Vocal \u00b7 Keys \u00b7 Pad \u00b7 FX", "the factory presets, by use"),
                ("My IR", "your own IRs, with Init's settings"),
                ("Back to Init", "whenever you pick one of your IRs")]
        for k, (c, b) in enumerate(best):
            y = 290 + k * 82
            d.text((900, y), c, font=F("s", 26), fill=AMB, anchor="ls")
            d.text((900, y + 30), b, font=F("r", 20), fill=GR, anchor="ls")
        d.text((900, 560), "(a menu drawn here, not a screenshot", font=F("r", 16), fill=DIM, anchor="ls")
        d.text((900, 580), " of MPC: the names are what it lists)", font=F("r", 16), fill=DIM, anchor="ls")
    else:
        header(d, 70, 110, "NEW IN 1.4", "In MPC's preset menu", body[:1], 940, 1.25)
        framed(im, program_list(440, 520, 13, 9, 12), (70, 440))
        framed(im, page_crop("stabs", (0, 120, 430, 300), 1.0), (540, 440))
        d.text((540, 650), "each IR says what it suits", font=F("r", 22), fill=GR, anchor="ls")
        wordmark(d, 640, 1010, 34)
    return im

# ---- the overview: 1.4 on one card (the README's "New in 1.4", a release or a post) --------------------------------------
FEATS = [("duck", "Duck", "The reverb ducks while you play and blooms in the gaps."),
         ("sync", "Tempo SYNC", "Pre-delay in note values, locked to your MPC's tempo."),
         ("textures", "Textures", "Five IRs that aren't rooms: a pipe, grains, a chord, a breath, a bounce."),
         ("presets", "MPC's preset menu", "The presets by use in MPC's own menu, and your own IRs too.")]

def make_overview(W, H, social=False):
    im = canvas(W, H); d = ImageDraw.Draw(im)
    wordmark(d, 60, 92 if not social else 110, 40 if not social else 46)
    k = "1.4"; d.text((W - 60, 92 if not social else 110), k, font=F("s", 44 if not social else 52), fill=AMB, anchor="rs")
    d.text((62, 132 if not social else 160), "What's new: one knob, one toggle, five new spaces, and your MPC's own preset menu.",
           font=F("r", 23 if not social else 26), fill=GR, anchor="ls") if not social else None
    if social:
        for i, ln in enumerate(wrap(d, "What's new: one knob, one toggle, five new spaces, and your MPC's own preset menu.", F("r", 26), W - 120)):
            d.text((62, 160 + i * 36), ln, font=F("r", 26), fill=GR, anchor="ls")
    cols, gap = (4, 24) if not social else (2, 40)
    tw = (W - 120 - (cols - 1) * gap) // cols if not social else 330
    th = int(tw * 560 / 720)
    gx = 60 if not social else (W - 2 * tw - gap) // 2
    gy = 186 if not social else 232
    for i, (name, title, line) in enumerate(FEATS):
        c, r = i % cols, i // cols
        x = gx + c * (tw + gap); y = gy + r * (th + (150 if not social else 140))
        thumb = Image.open(os.path.join(OUT, "feature-%s.png" % name)).convert("RGB").crop((560, 40, 1280, 600)).resize((tw, th), Image.LANCZOS)
        framed(im, thumb, (x, y))
        d.text((x, y + th + 46), title, font=F("s", 30 if not social else 32), fill=IV, anchor="ls")
        for j, ln in enumerate(wrap(d, line, F("r", 20 if not social else 22), tw)):
            d.text((x, y + th + 80 + j * (28 if not social else 29)), ln, font=F("r", 20 if not social else 22), fill=GR, anchor="ls")
    foot = "Free  \u00b7  Akai MPC and Force, Gen 1 and Gen 2  \u00b7  github.com/gmorb/mpc-vst-liminal-hz"
    d.text((W // 2, H - 30), foot, font=F("r", 19 if not social else 22), fill=DIM, anchor="ms")
    return im

def main():
    social = sys.argv[1] if len(sys.argv) > 1 else None
    for name, fn in (("duck", make_duck), ("sync", make_sync), ("textures", make_textures), ("presets", make_presets)):
        fn(1280, 640).save(os.path.join(OUT, "feature-%s.png" % name), optimize=True)
        if social:
            os.makedirs(social, exist_ok=True)
            fn(1080, 1080, True).save(os.path.join(social, "liminal-hz-%s-1080.png" % name), optimize=True)
        print("feature", name)
    make_overview(1280, 640).save(os.path.join(OUT, "whats-new.png"), optimize=True)   # (after the four: it shows them)
    if social: make_overview(1080, 1080, True).save(os.path.join(social, "liminal-hz-1.4-whats-new-1080.png"), optimize=True)
    print("overview")

if __name__ == "__main__":
    main()
