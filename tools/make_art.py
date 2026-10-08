#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_art.py -- the page's generated artwork, into vst/art/:
  grain.png         1280x628: near-black grain texture (spare; the page background is make_backgrounds.py's threshold_air)
  wave_0..7.png     22x88: the waveform's bars, 8 heights (0 = a faint baseline tick)
  play_0..24.png    552x12: the playhead strip (0 = hidden; k = a marker at column k, faint trail behind)
Deterministic (fixed seeds), so the build is reproducible."""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "vst", "art")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1977)
COLS, BAR_W, BAR_H, STEP = 24, 22, 88, 23
AMBER = np.array([184, 137, 74]); AMBER_HI = np.array([232, 211, 160])
BONE = np.array([150, 143, 130]); BONE_HI = np.array([214, 206, 190])   # the waveform: neutral (amber = live)

# ---- texture ---------------------------------------------------------------------------------------------------------
W, H = 1280, 628
base = np.full((H, W), 11.0)
def blur_noise(scale, amp):
    small = rng.normal(0, 1, (H // scale + 2, W // scale + 2))
    img = Image.fromarray(((small - small.min()) / (np.ptp(small) + 1e-9) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
    return (np.asarray(img, float) / 255 - 0.5) * amp
base += blur_noise(90, 9) + blur_noise(25, 5) + rng.normal(0, 3.2, (H, W))           # blotches + grain
img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).convert("RGB")
d = ImageDraw.Draw(img)
for _ in range(38):                                                                    # hairline scratches
    x, y = rng.uniform(0, W), rng.uniform(0, H); ang = rng.uniform(-0.5, 0.5) + (math.pi / 2 if rng.random() < 0.3 else 0)
    ln = rng.uniform(40, 260); c = int(rng.uniform(22, 34))
    d.line([(x, y), (x + ln * math.cos(ang), y + ln * math.sin(ang))], fill=(c, c, c + 2), width=1)
a = np.asarray(img, float)
yy, xx = np.mgrid[0:H, 0:W]; r = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)
a *= (1 - 0.45 * np.clip(r - 0.35, 0, 1) ** 1.6)[..., None]                              # vignette
a[..., 2] *= 1.04; a[..., 0] *= 0.97                                                   # a cold, slightly odd cast
Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(os.path.join(OUT, "grain.png"))   # (the page uses texture.png from make_backgrounds.py: threshold_air)

# ---- waveform bars ---------------------------------------------------------------------------------------------------
for lv in range(8):
    im = Image.new("RGBA", (BAR_W, BAR_H), (0, 0, 0, 0)); px = im.load()
    h = 2 if lv == 0 else int(round(BAR_H * lv / 7))
    for y in range(BAR_H - h, BAR_H):
        t = (BAR_H - y) / BAR_H
        col = BONE + (BONE_HI - BONE) * t * (0.7 if lv else 0)
        for x in range(2, BAR_W - 2):
            rough = 1 if (x in (2, BAR_W - 3) and (y * 7 + x) % 5 == 0) else 0             # slightly rough edges
            alpha = 70 if lv == 0 else (230 - rough * 120)
            px[x, y] = (int(col[0]), int(col[1]), int(col[2]), alpha)
    im.save(os.path.join(OUT, "wave_%d.png" % lv))

# ---- playhead strip --------------------------------------------------------------------------------------------------
SW, SH = COLS * STEP, 12
for k in range(COLS + 1):
    im = Image.new("RGBA", (SW, SH), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    if k:
        x0 = (k - 1) * STEP
        for j in range(1, 4):                                                            # faint trail
            xt = x0 - j * STEP
            if xt >= 0: d.rectangle([xt + 2, 4, xt + BAR_W - 3, SH - 4], fill=(184, 137, 74, 70 - j * 18))
        d.rectangle([x0 + 1, 1, x0 + BAR_W - 2, SH - 2], fill=(232, 211, 160, 255))
    im.save(os.path.join(OUT, "play_%d.png" % k))
print("art written to", OUT)

# ---- knobs (filmstrips: 128 frames, minimum first) ------------------------------------------------------------------
# Matte graphite body lit from the top left, an engraved tick scale, an ivory pointer, and the value as a lit amber
# arc: amber means "live value" on this page (the playhead is the only other amber).
from PIL import ImageFilter as _IF
def knob_strip(path, size, ticks, frames=128, ss=4):
    S = size * ss; c = S / 2
    def P(r, deg):                                                       # deg: 0 = up, clockwise
        a = math.radians(deg - 90); return (c + r * math.cos(a), c + r * math.sin(a))
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(base)
    for k in range(ticks):                                               # engraved scale
        deg = -135 + 270 * k / (ticks - 1); major = k in (0, ticks // 2, ticks - 1)
        r0, r1 = (0.425 if major else 0.44) * S, 0.475 * S
        d.line([P(r0, deg), P(r1, deg)], fill=(96, 90, 82, 255) if major else (66, 62, 57, 255), width=int(S * (0.012 if major else 0.008)))
    track_r, track_w = 0.395 * S, int(S * 0.028)
    d.arc([c - track_r, c - track_r, c + track_r, c + track_r], 135, 405, fill=(24, 24, 27, 255), width=track_w)
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0)); ds = ImageDraw.Draw(sh)  # contact shadow under the body
    br = 0.33 * S; ds.ellipse([c - br, c - br + S * 0.03, c + br, c + br + S * 0.03], fill=(0, 0, 0, 170))
    sh = sh.filter(_IF.GaussianBlur(S * 0.03)); base = Image.alpha_composite(base, sh)
    body = Image.new("RGBA", (S, S), (0, 0, 0, 0)); px = np.zeros((S, S, 4), np.uint8)
    yy, xx = np.mgrid[0:S, 0:S]; dx, dy = (xx - c) / br, (yy - c) / br; rr = np.sqrt(dx * dx + dy * dy)
    light = np.clip(0.55 - 0.32 * (dx * 0.7 + dy * 0.7) / np.maximum(rr, 1e-6) * np.minimum(rr, 1), 0, 1)   # top-left key light
    tone = 20 + 26 * light * (1 - 0.35 * rr ** 2) + 2.5 * np.sin(rr * 120)                                      # faint machining
    inside = rr <= 1.0
    for ch, k in enumerate((1.0, 1.0, 1.06)): px[..., ch] = np.clip(tone * k, 0, 255) * inside
    px[..., 3] = (inside * 255).astype(np.uint8)
    rim = (rr > 0.965) & (rr <= 1.0) & (dy < 0)                                                                  # a thin catch-light rim, top
    px[rim, :3] = np.clip(px[rim, :3].astype(int) + 22, 0, 255)
    body = Image.fromarray(px, "RGBA"); base = Image.alpha_composite(base, body)
    sheet = Image.new("RGBA", (size, size * frames), (0, 0, 0, 0))
    for f in range(frames):
        v = f / (frames - 1); im = base.copy(); d = ImageDraw.Draw(im)
        end = 135 + 270 * v
        if v > 0.002:
            glow = Image.new("RGBA", (S, S), (0, 0, 0, 0)); dg = ImageDraw.Draw(glow)
            dg.arc([c - track_r, c - track_r, c + track_r, c + track_r], 135, end, fill=(226, 170, 90, 120), width=track_w * 2)
            im = Image.alpha_composite(im, glow.filter(_IF.GaussianBlur(S * 0.018))); d = ImageDraw.Draw(im)
            d.arc([c - track_r, c - track_r, c + track_r, c + track_r], 135, end, fill=(214, 164, 92, 255), width=track_w)
        deg = -135 + 270 * v                                              # the pointer: ivory, round caps
        p0, p1 = P(0.12 * S, deg), P(0.30 * S, deg); w = int(S * 0.034)
        d.line([p0, p1], fill=(236, 228, 212, 255), width=w)
        for p in (p0, p1): d.ellipse([p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2], fill=(236, 228, 212, 255))
        sheet.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    sheet.save(path, optimize=True)

knob_strip(os.path.join(OUT, "knob_big.png"), 160, 31)
knob_strip(os.path.join(OUT, "knob_small.png"), 96, 11)

# ---- the wordmark: LIMINAL Hz, spaced, with the maker's mark -------------------------------------------------------
from PIL import ImageFont as _IFont
FONTS = "/home/claude/mpc-vst-plugins/tools/html_art/fonts/"
wm = Image.new("RGBA", (600, 72), (0, 0, 0, 0)); d = ImageDraw.Draw(wm)
f_big = _IFont.truetype(FONTS + "TitilliumWeb-Regular.ttf", 44); f_hz = _IFont.truetype(FONTS + "TitilliumWeb-SemiBold.ttf", 44)
f_by = _IFont.truetype(FONTS + "TitilliumWeb-Regular.ttf", 23)   # the maker's mark: readable
x = 0
for ch in "LIMINAL":
    d.text((x, 4), ch, font=f_big, fill=(205, 197, 182, 255)); x += d.textlength(ch, font=f_big) + 9   # wide tracking
x += 12
d.text((x, 4), "Hz", font=f_hz, fill=(236, 228, 212, 255)); x += d.textlength("Hz", font=f_hz) + 18
d.text((x, 26), "by Gm0rb", font=f_by, fill=(140, 134, 124, 255))
wm.save(os.path.join(OUT, "wordmark.png"))
print("knobs and wordmark written")

# ---- round 3: the room's strands (level x tint), the threshold background, hand-drawn annotations --------------------
STRAND_W, STRAND_H = 22, 96
TINTS = [(118, 92, 66), (172, 164, 150), (186, 204, 216)]            # dark (warm umber), balanced (bone), bright (pale cool)
def strand(level, tint, seed):
    g = np.random.default_rng(seed)
    im = np.zeros((STRAND_H, STRAND_W, 4), float)
    if level == 0:
        im[-2:, 3:STRAND_W - 3, :3] = TINTS[tint]; im[-2:, 3:STRAND_W - 3, 3] = 60
        return Image.fromarray(im.astype(np.uint8), "RGBA")
    h = STRAND_H * level / 7
    xs = np.arange(STRAND_W)
    edge = 3 + g.normal(0, 0.6, STRAND_H).cumsum() * 0.08                # slightly wandering edges
    streak = 0.8 + 0.2 * g.random(STRAND_W)                              # faint vertical streaks
    for y in range(STRAND_H):
        top = STRAND_H - h
        if y < top - 10: continue
        a = np.clip((y - (top - 10)) / 22, 0, 1) ** 1.4                  # a feathered top: mist, not a block
        e = edge[y]
        inside = np.clip(np.minimum(xs - e, (STRAND_W - 1 - e) - xs) / 2.5, 0, 1)
        im[y, :, :3] = TINTS[tint]
        im[y, :, 3] = 200 * a * inside * streak
    return Image.fromarray(np.clip(im, 0, 255).astype(np.uint8), "RGBA")

for t in range(3):
    for lv in range(8):
        strand(lv, t, 100 + t * 8 + lv).save(os.path.join(OUT, "wave_%d.png" % (lv + 8 * t)))

# hand-drawn annotations: lowercase, slightly tilted words and pencil brackets that wobble a little
def pencil(d, pts, rng_, width=2, col=(150, 143, 132, 150)):
    out = []
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = max(2, int(math.hypot(x1 - x0, y1 - y0) / 6))
        for k in range(n):
            t = k / n; out.append((x0 + (x1 - x0) * t + rng_.normal(0, 0.45), y0 + (y1 - y0) * t + rng_.normal(0, 0.45)))
    out.append(pts[-1])
    d.line(out, fill=col, width=width, joint="curve")

def annotations(path, items, brackets):
    im = Image.new("RGBA", (1280, 628), (0, 0, 0, 0))
    f = _IFont.truetype(FONTS + "TitilliumWeb-Regular.ttf", 19)
    g = np.random.default_rng(5)
    for (x, y, text, tilt) in items:                                   # each word a touch off-level
        w_ = int(f.getlength(text) + text.count(" ") * 0 + len(text) * 4 + 10)
        lay = Image.new("RGBA", (w_, 34), (0, 0, 0, 0)); dl = ImageDraw.Draw(lay); xx_ = 4
        for ch in text: dl.text((xx_, 4), ch, font=f, fill=(152, 145, 134, 235)); xx_ += f.getlength(ch) + 4
        lay = lay.rotate(tilt, resample=Image.BICUBIC, expand=True)
        im.alpha_composite(lay, (int(x), int(y)))
    d = ImageDraw.Draw(im)
    for (x0, x1, y) in brackets:                                       # a bracket: hooks down at both ends
        pencil(d, [(x0, y + 7), (x0 + 3, y), (x1 - 3, y + g.normal(0, 0.8)), (x1, y + 7)], g)
    im.save(path)

# positions on the page (page y = layout y - 86): see vst/layout.conf
annotations(os.path.join(OUT, "annotations.png"),
            [(32, 6, "ir", -0.6), (646, 6, "the room", 0.5), (258, 368, "time", -0.8), (580, 380, "tone", 0.6), (846, 368, "space", -0.4)],
            [(242, 486, 404), (544, 782, 418), (828, 1062, 404)])
print("strands and annotations written")

# ---- the SHAPE display, v2: 16 wide columns, each a height (0..7) and a tone (dark, balanced, airy) ---------------
# What you see is how the shaped IR sounds: tall = loud, ice = airy (high-frequency energy), bone = balanced,
# dusk = dark. A reverb that darkens as it decays shows its colour fading from ice to dusk.
COLS2, BW, BH, STEP2 = 16, 44, 130, 47
TONES = {0: ((94, 86, 104), (138, 128, 150)),      # dark: dusky violet-grey
         1: ((150, 143, 130), (214, 206, 190)),    # balanced: bone
         2: ((160, 178, 186), (232, 242, 244))}    # airy: pale ice
for tone, (lo_c, hi_c) in TONES.items():
    for lv in range(8):
        im = Image.new("RGBA", (BW, BH), (0, 0, 0, 0)); px = im.load()
        h = 3 if lv == 0 else int(round((BH - 14) * lv / 7))
        for y in range(BH - h, BH):
            t = (BH - y) / BH
            c = [int(lo_c[k] + (hi_c[k] - lo_c[k]) * t) for k in range(3)]
            for x in range(3, BW - 3):
                edge = x in (3, BW - 4)
                px[x, y] = (c[0], c[1], c[2], 55 if lv == 0 else (150 if edge else 225))
        if tone == 2 and lv > 0:                                          # air: a glow and a few motes above the top
            glow = Image.new("RGBA", (BW, BH), (0, 0, 0, 0)); dg = ImageDraw.Draw(glow)
            top = BH - h
            dg.ellipse([4, top - 10, BW - 5, top + 8], fill=(232, 242, 244, 70))
            glow = glow.filter(ImageFilter.GaussianBlur(4)); im = Image.alpha_composite(glow, im)
            d = ImageDraw.Draw(im); g = np.random.default_rng(lv)
            for _ in range(2 + lv // 3):
                x0, y0 = g.uniform(8, BW - 8), top - g.uniform(4, 13)
                if y0 > 1: d.ellipse([x0 - 1, y0 - 1, x0 + 1, y0 + 1], fill=(240, 248, 250, 200))
        im.save(os.path.join(OUT, "wave2_%d.png" % (tone * 8 + lv)))
SW2 = COLS2 * STEP2 - 3
for k in range(COLS2 + 1):                                                 # the playhead: 16 positions + hidden
    im = Image.new("RGBA", (SW2, 12), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    if k:
        x0 = (k - 1) * STEP2
        for j in range(1, 3):
            xt = x0 - j * STEP2
            if xt >= 0: d.rectangle([xt + 4, 4, xt + BW - 5, 8], fill=(214, 164, 92, 60 - j * 20))
        d.rounded_rectangle([x0 + 2, 1, x0 + BW - 3, 10], radius=3, fill=(226, 178, 104, 255))
    im.save(os.path.join(OUT, "play2_%d.png" % k))
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
f_cap = _IFont.truetype(MONO, 15)
ru = Image.new("RGBA", (SW2, 24), (0, 0, 0, 0)); d = ImageDraw.Draw(ru)   # the ruler under the display
for k in range(COLS2 + 1):
    x = min(SW2 - 1, k * STEP2 - (2 if k else 0))
    d.line([(x, 0), (x, 6 if k % 4 else 10)], fill=(110, 104, 95, 255), width=1)
d.text((2, 11), "start", font=f_cap, fill=(110, 104, 95, 255))
d.text((SW2 - 2, 11), "end", font=f_cap, fill=(110, 104, 95, 255), anchor="ra")
d.text((SW2 // 2, 11), "\u2192 tail", font=f_cap, fill=(90, 85, 78, 255), anchor="ma")
ru.save(os.path.join(OUT, "ruler.png"))
for word in ("shape", "trim", "fade", "time", "space", "tone", "out", "tone3000", "ir"):
    tw = int(d.textlength(word, font=f_cap)) + 4
    cap = Image.new("RGBA", (tw, 20), (0, 0, 0, 0)); dc = ImageDraw.Draw(cap)
    dc.text((1, 1), word, font=f_cap, fill=(122, 116, 106, 255))
    cap.save(os.path.join(OUT, "cap_%s.png" % word))
print("SHAPE v2 art written")

# ---- the SHAPE display, v3: an impulse-response painting and a travelling glow ------------------------------------
# Each column: a dense jagged trace (audio, zoomed in) wrapped in mist of the same shape; height = level, colour and
# softness = tone (air: pale ice, light mist, sparkles; warm: bone haze; dark: dusky violet, muffled heavy blur).
# Columns are 42 px with no gap, so they join into one surface. The glow: a soft amber orb (amber = live) with a
# shimmer of light through the painting, a trail of mist behind it and a few motes; 32 positions + hidden.
COLS3, CW3, CH3 = 16, 44, 160                                          # 44 wide, placed 42 apart: they overlap 2 px (no rounding gaps)
MID3 = CH3 // 2
TONES3 = {0: dict(core=(128, 118, 146), mist=(70, 62, 88), blur=9, mist_a=120, core_a=120, sparkle=0),
          1: dict(core=(214, 204, 186), mist=(120, 112, 98), blur=6, mist_a=110, core_a=170, sparkle=0),
          2: dict(core=(226, 240, 246), mist=(140, 164, 176), blur=5, mist_a=120, core_a=200, sparkle=1)}
for tone, T in TONES3.items():
    for lv in range(8):
        g = np.random.default_rng(1000 + tone * 8 + lv)
        A = (CH3 / 2 - 8) * lv / 7.0
        core = Image.new("RGBA", (CW3, CH3), (0, 0, 0, 0)); dc = ImageDraw.Draw(core)
        PAD = 24                                                          # mist drawn past the edges, blurred, cropped:
        mist = Image.new("RGBA", (CW3 + 2 * PAD, CH3), (0, 0, 0, 0)); dm = ImageDraw.Draw(mist)   # no seams between columns
        if lv == 0:
            dc.line([(0, MID3), (CW3, MID3)], fill=T["core"] + (55,), width=1)        # silence: a faint thread
        else:
            for x in range(CW3):
                r = A * min(1.0, 0.30 + 0.70 * abs(g.normal(0, 0.55)))           # jagged: noise under an envelope
                up, dn = r * g.uniform(0.75, 1.0), r * g.uniform(0.75, 1.0)
                a = int(T["core_a"] * g.uniform(0.55, 1.0))
                dc.line([(x, MID3 - up), (x, MID3 + dn)], fill=T["core"] + (a,), width=1)
            for x in range(CW3 + 2 * PAD):                                    # the mist: the same envelope, wider canvas
                rm = min(CH3 / 2 - 2, A * min(1.0, 0.30 + 0.70 * abs(g.normal(0, 0.55))) * 1.35 + 3)
                dm.line([(x, MID3 - rm), (x, MID3 + rm)], fill=T["mist"] + (T["mist_a"],), width=1)
        mist = mist.filter(ImageFilter.GaussianBlur(T["blur"])).crop((PAD, 0, PAD + CW3, CH3))
        im = Image.alpha_composite(mist, core)
        if T["sparkle"] and lv > 0:                                               # air: sparkles at the edges
            d = ImageDraw.Draw(im)
            for _ in range(3 + lv):
                x0 = g.uniform(1, CW3 - 2); side = 1 if g.random() < 0.5 else -1
                y0 = MID3 + side * (A * g.uniform(0.8, 1.25) + g.uniform(2, 9))
                if 1 < y0 < CH3 - 2: d.ellipse([x0 - 0.9, y0 - 0.9, x0 + 0.9, y0 + 0.9], fill=(244, 250, 252, int(g.uniform(120, 230))))
        im.save(os.path.join(OUT, "wave3_%d.png" % (tone * 8 + lv)))
GW3, STEPS3 = COLS3 * 42, 32
for k in range(STEPS3 + 1):                                                    # the travelling glow
    im = Image.new("RGBA", (GW3, CH3), (0, 0, 0, 0))
    if k:
        cx = (k - 0.5) / STEPS3 * GW3; g = np.random.default_rng(500 + k)
        glow = Image.new("RGBA", (GW3, CH3), (0, 0, 0, 0)); dg = ImageDraw.Draw(glow)
        for j, (dx, a, r) in enumerate(((0, 150, 30), (-26, 70, 26), (-54, 40, 22), (-86, 20, 18))):   # orb + trail
            dg.ellipse([cx + dx - r, MID3 - r * 1.25, cx + dx + r, MID3 + r * 1.25], fill=(226, 172, 98, a))
        dg.rectangle([cx - 2, 6, cx + 2, CH3 - 6], fill=(240, 214, 160, 60))      # a shimmer through the painting
        glow = glow.filter(ImageFilter.GaussianBlur(11))
        im = Image.alpha_composite(im, glow); d = ImageDraw.Draw(im)
        d.ellipse([cx - 5, MID3 - 5, cx + 5, MID3 + 5], fill=(250, 226, 178, 230))   # the bright heart
        for _ in range(7):                                                     # motes drifting behind
            x0 = cx - g.uniform(4, 90); y0 = MID3 + g.normal(0, 26)
            if 0 < x0 < GW3 and 2 < y0 < CH3 - 2: d.ellipse([x0 - 1.1, y0 - 1.1, x0 + 1.1, y0 + 1.1], fill=(244, 214, 160, int(g.uniform(70, 170))))
    im.save(os.path.join(OUT, "glow3_%d.png" % k))
f_cap2 = _IFont.truetype(MONO, 19)                                             # captions, bigger
for word in ("shape", "trim", "fade", "time", "space", "tone", "out"):
    tw = int(ImageDraw.Draw(Image.new("RGBA", (4, 4))).textlength(word, font=f_cap2)) + 4
    cap = Image.new("RGBA", (tw, 26), (0, 0, 0, 0)); dc = ImageDraw.Draw(cap)
    dc.text((1, 2), word, font=f_cap2, fill=(128, 122, 112, 255))
    cap.save(os.path.join(OUT, "cap2_%s.png" % word))
print("SHAPE v3 art written")

# ---- the SHAPE display, v4 (device-safe): 12 columns, edge to edge, no sparkles; a 24-step glow ---------------------
# v3 didn't show on the Force; v4 keeps to what's proven there: pictures that don't overlap, at most 25 states each.
# Air is no longer sparkles: a paler, more translucent, wider mist.
COLS4, CW4, CH4 = 12, 52, 150   # 52 px, placed 56 apart: the kit pads each picture 2 px (opaque), so neighbours need a gap
MID4 = CH4 // 2
TONES4 = {0: dict(core=(128, 118, 146), mist=(70, 62, 88), blur=9, mist_a=120, core_a=120, spread=1.30),
          1: dict(core=(214, 204, 186), mist=(120, 112, 98), blur=7, mist_a=110, core_a=170, spread=1.35),
          2: dict(core=(222, 236, 242), mist=(150, 172, 184), blur=10, mist_a=58, core_a=135, spread=1.35)}
for tone, T in TONES4.items():
    for lv in range(8):
        g = np.random.default_rng(2000 + tone * 8 + lv)
        A = (CH4 / 2 - 8) * lv / 7.0
        core = Image.new("RGBA", (CW4, CH4), (0, 0, 0, 0)); dc = ImageDraw.Draw(core)
        PAD = 28
        mist = Image.new("RGBA", (CW4 + 2 * PAD, CH4), (0, 0, 0, 0)); dm = ImageDraw.Draw(mist)
        if lv == 0:
            dc.line([(0, MID4), (CW4, MID4)], fill=T["core"] + (55,), width=1)
        else:
            for x in range(CW4):
                r = A * min(1.0, 0.30 + 0.70 * abs(g.normal(0, 0.55)))
                up, dn = r * g.uniform(0.75, 1.0), r * g.uniform(0.75, 1.0)
                dc.line([(x, MID4 - up), (x, MID4 + dn)], fill=T["core"] + (int(T["core_a"] * g.uniform(0.55, 1.0)),), width=1)
            for x in range(CW4 + 2 * PAD):
                rm = min(CH4 / 2 - 2, A * min(1.0, 0.30 + 0.70 * abs(g.normal(0, 0.55))) * T["spread"] + 3)
                dm.line([(x, MID4 - rm), (x, MID4 + rm)], fill=T["mist"] + (T["mist_a"],), width=1)
        mist = mist.filter(ImageFilter.GaussianBlur(T["blur"])).crop((PAD, 0, PAD + CW4, CH4))
        fade = np.clip(1 - (np.abs(np.arange(CH4) - MID4) / (CH4 / 2)) ** 3, 0, 1)   # mist thins towards the edges:
        ma = np.asarray(mist, float); ma[..., 3] *= fade[:, None]                  # no hard top or bottom
        mist = Image.fromarray(ma.astype(np.uint8), "RGBA")
        Image.alpha_composite(mist, core).save(os.path.join(OUT, "wave4_%d.png" % (tone * 8 + lv)))
GW4, STEPS4 = COLS4 * CW4, 24
for k in range(STEPS4 + 1):
    im = Image.new("RGBA", (GW4, CH4), (0, 0, 0, 0))
    if k:
        cx = (k - 0.5) / STEPS4 * GW4; g = np.random.default_rng(600 + k)
        glow = Image.new("RGBA", (GW4, CH4), (0, 0, 0, 0)); dg = ImageDraw.Draw(glow)
        for dx, a, r in ((0, 150, 32), (-30, 70, 28), (-62, 40, 24), (-98, 20, 20)):
            dg.ellipse([cx + dx - r, MID4 - r * 1.2, cx + dx + r, MID4 + r * 1.2], fill=(226, 172, 98, a))
        dg.rectangle([cx - 2, 6, cx + 2, CH4 - 6], fill=(240, 214, 160, 60))
        im = Image.alpha_composite(im, glow.filter(ImageFilter.GaussianBlur(12)))
        ImageDraw.Draw(im).ellipse([cx - 5, MID4 - 5, cx + 5, MID4 + 5], fill=(250, 226, 178, 230))
    im.save(os.path.join(OUT, "glow4_%d.png" % k))
print("SHAPE v4 art written")

# ---- the glow, v5: its own strip under the painting (the kit's pictures are opaque: overlaid on the painting, the
# glow hid it). A faint line, and a misty amber light drifting along it with a trail; 24 positions + hidden.
GW5, GH5, STEPS5 = 668, 30, 24
for k in range(STEPS5 + 1):
    im = Image.new("RGBA", (GW5, GH5), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.line([(0, GH5 // 2), (GW5, GH5 // 2)], fill=(110, 104, 95, 70), width=1)
    if k:
        cx = (k - 0.5) / STEPS5 * GW5
        glow = Image.new("RGBA", (GW5, GH5), (0, 0, 0, 0)); dg = ImageDraw.Draw(glow)
        for dx, a, rx in ((0, 170, 22), (-30, 80, 22), (-62, 40, 20), (-96, 18, 18)):
            dg.ellipse([cx + dx - rx, GH5 / 2 - 9, cx + dx + rx, GH5 / 2 + 9], fill=(226, 172, 98, a))
        im = Image.alpha_composite(im, glow.filter(ImageFilter.GaussianBlur(6)))
        ImageDraw.Draw(im).ellipse([cx - 4, GH5 / 2 - 4, cx + 4, GH5 / 2 + 4], fill=(250, 226, 178, 235))
    im.save(os.path.join(OUT, "glow5_%d.png" % k))
print("glow v5 written")

# ---- the panel the IR List and My Presets open on (laid over the browser column, 540x314) ---------------------------
pn = Image.new("RGBA", (540, 314), (0, 0, 0, 0)); dp = ImageDraw.Draw(pn)
dp.rounded_rectangle((0, 0, 539, 313), radius=8, fill=(11, 12, 14, 255), outline=(58, 55, 50, 255), width=2)
dp.rounded_rectangle((4, 4, 535, 309), radius=6, outline=(26, 26, 29, 255), width=1)
pn.save(os.path.join(OUT, "menu_panel.png"))
print("menu panel written")

# ---- the IR stepper's arrows (66x66). They are drawn into the page's own background, not the stepper's gated part: the
# skin takes each arrow's tap-zone picture from the background, so an arrow drawn only while the browser shows would vanish.
for name, pts in (("arrow_prev", [(40, 17), (40, 49), (21, 33)]), ("arrow_next", [(26, 17), (26, 49), (45, 33)])):
    ar = Image.new("RGBA", (66, 66), (0, 0, 0, 0)); da = ImageDraw.Draw(ar)
    da.rounded_rectangle((0, 0, 65, 65), radius=5, fill=(29, 31, 35, 255), outline=(44, 44, 49, 255), width=1)
    da.polygon(pts, fill=(184, 137, 74, 255))
    ar.save(os.path.join(OUT, name + ".png"))
print("stepper arrows written")
