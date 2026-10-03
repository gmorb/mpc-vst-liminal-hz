#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_backgrounds.py -- candidate page backgrounds (1280x628) for Liminal Hz, into vst/art/bg_<name>.png.
All dark and low-contrast (the page's controls and MPC's live text sit on top), grain-finished, deterministic.
  threshold     corridor lines converging on a far doorway with a faint glow
  fluorescent   cold strip-light haze from the top edge, faint scanlines, a green-yellow cast
  interference  two unseen wave sources overlapping into soft moire bands ("Hz")
  poolrooms     a tiled floor receding into fog, muted cyan
The chosen one is copied to art/texture.png (the page's bottom layer)."""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 1280, 628
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "vst", "art")
rng = np.random.default_rng(2026)
yy, xx = np.mgrid[0:H, 0:W].astype(float)


def finish(rgb, grain=2.6, vignette=0.5):
    """grain, vignette, clamp to a dark range"""
    r = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)
    rgb = rgb * (1 - vignette * np.clip(r - 0.3, 0, 1) ** 1.5)[..., None]
    rgb = rgb + rng.normal(0, grain, (H, W, 1))
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))


def lines_layer(segments, alpha, width=1, blur=0.6):
    im = Image.new("L", (W, H), 0); d = ImageDraw.Draw(im)
    for (x0, y0, x1, y1) in segments: d.line([(x0, y0), (x1, y1)], fill=alpha, width=width)
    return np.asarray(im.filter(ImageFilter.GaussianBlur(blur)), float)


def threshold():
    vx, vy = 900, 300                                                     # the far doorway, right of centre
    base = np.full((H, W, 3), 9.0)
    glow = np.exp(-(((xx - vx) / 95) ** 2 + ((yy - vy) / 130) ** 2))     # light spilling from the doorway
    base += glow[..., None] * np.array([22, 20, 17])
    dw, dh = 54, 92                                                       # doorway frame
    segs = [(vx - dw, vy - dh, vx + dw, vy - dh), (vx - dw, vy + dh, vx + dw, vy + dh),
            (vx - dw, vy - dh, vx - dw, vy + dh), (vx + dw, vy - dh, vx + dw, vy + dh)]
    for (cx, cy) in [(vx - dw, vy - dh), (vx + dw, vy - dh), (vx - dw, vy + dh), (vx + dw, vy + dh)]:   # corridor edges
        ex = cx + (cx - vx) * 30; ey = cy + (cy - vy) * 30
        segs.append((cx, cy, ex, ey))
    for k in range(1, 9):                                                 # receding wall seams
        t = 1 + 0.55 * k ** 1.35
        x0, x1 = vx - dw * t, vx + dw * t; y0, y1 = vy - dh * t, vy + dh * t
        segs += [(x0, y0, x0, y1), (x1, y0, x1, y1)]
    L = lines_layer(segs, 120, width=2, blur=0.9)
    near = np.clip(np.sqrt((xx - vx) ** 2 + (yy - vy) ** 2) / 700, 0.25, 1)   # lines fade towards the light
    base += (L / 255 * near)[..., None] * np.array([30, 29, 27])
    return finish(base, grain=2.4, vignette=0.55)


def fluorescent():
    base = np.full((H, W, 3), 8.0)
    for cx, ln in ((380, 420), (980, 380)):                               # two tubes just above the page
        d = np.sqrt(np.maximum(np.abs(xx - cx) - ln / 2, 0) ** 2 + (yy + 30) ** 2)
        base += (np.exp(-d / 210) * 34)[..., None] * np.array([0.92, 1.0, 0.80])
    base *= (1 + 0.035 * np.sin(yy * 2 * math.pi / 3))[..., None]        # faint scanlines
    band = 1 + 0.06 * np.sin(yy / 41.0 + 1.3) * np.exp(-yy / 400)       # a slow flicker band
    base *= band[..., None]
    return finish(base, grain=2.8, vignette=0.45)


def interference():
    s1, s2 = (-220, 420), (1500, 120)                                     # sources off the page
    r1 = np.sqrt((xx - s1[0]) ** 2 + (yy - s1[1]) ** 2); r2 = np.sqrt((xx - s2[0]) ** 2 + (yy - s2[1]) ** 2)
    k = 2 * math.pi / 46
    wave = np.cos(k * r1) + np.cos(k * 1.06 * r2)                         # slightly different "frequencies": beats
    env = 0.5 + 0.5 * np.cos(k * 0.06 * (r1 - r2))                        # the beat envelope: soft bands
    v = (wave * env + 2) / 4                                              # 0..1
    v = np.asarray(Image.fromarray((v * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), float) / 255
    base = 9 + v[..., None] * np.array([13, 11, 18])                      # a cold violet-grey
    return finish(base, grain=2.4, vignette=0.6)


def poolrooms():
    base = np.full((H, W, 3), 8.0) + np.array([0, 2, 3])
    hy = 250                                                              # horizon
    segs = []
    vx = 640
    for i in range(-40, 41):                                              # tile lines towards the vanishing point
        x = vx + i * 70
        segs.append((vx + i * 4, hy, x + (x - vx) * 2.2, H + 400))
    for k in range(1, 22):                                                # cross lines, closer together near the horizon
        y = hy + 6 * k ** 1.55
        if y < H: segs.append((0, y, W, y))
    L = lines_layer(segs, 140, width=2, blur=0.9)
    fog = np.clip((yy - hy) / (H - hy), 0, 1) ** 0.7                      # lines appear out of the fog
    base += (L / 255 * fog * 26)[..., None] * np.array([0.75, 1.0, 1.0])
    base += (np.exp(-((yy - hy) / 90) ** 2) * 14)[..., None] * np.array([0.8, 1.0, 1.05])   # haze at the horizon
    return finish(base, grain=2.6, vignette=0.5)


# ---- round 2: spaces you could hear -------------------------------------------------------------------------------
def fractal(scale, octaves=4, seed=0):
    """multi-octave value noise in 0..1 (smooth, organic)"""
    g = np.random.default_rng(seed); acc = np.zeros((H, W)); amp = 1.0; tot = 0
    for o in range(octaves):
        s_ = max(2, int(scale / 2 ** o))
        small = g.random((H // s_ + 3, W // s_ + 3))
        img = Image.fromarray((small * 255).astype(np.uint8)).resize(((W // s_ + 3) * s_, (H // s_ + 3) * s_), Image.BICUBIC)
        acc += amp * np.asarray(img, float)[:H, :W] / 255; tot += amp; amp *= 0.5
    return acc / tot


def frame_lines(rects, alpha, width=2, blur=0.9):
    segs = []
    for (x0, y0, x1, y1) in rects: segs += [(x0, y0, x1, y0), (x0, y1, x1, y1), (x0, y0, x0, y1), (x1, y0, x1, y1)]
    return lines_layer(segs, alpha, width, blur)


def threshold_deep():
    vx, vy = 870, 300
    base = np.full((H, W, 3), 8.0)
    glow = np.exp(-(((xx - vx) / 70) ** 2 + ((yy - vy) / 95) ** 2))
    base += glow[..., None] * np.array([40, 34, 26])                       # a warm, far light
    rects = []
    for k in range(9):                                                    # doorway after doorway, receding
        t = 0.55 * 1.42 ** k
        rects.append((vx - 60 * t, vy - 100 * t, vx + 60 * t, vy + 100 * t))
    L = frame_lines(rects, 150)
    near = np.clip(np.sqrt((xx - vx) ** 2 + (yy - vy) ** 2) / 600, 0.35, 1)
    base += (L / 255 * near)[..., None] * np.array([40, 37, 33])
    return finish(base, grain=2.5, vignette=0.5)


def threshold_open():
    base = np.full((H, W, 3), 8.0)
    dx0, dx1, dy0, dy1 = 250, 330, 120, 330                               # a door, left third
    door = (xx > dx0) & (xx < dx1) & (yy > dy0) & (yy < dy1)
    base[door] += np.array([30, 27, 22])
    floor = yy > dy1                                                       # its light falls across the floor
    spread = (yy - dy1) * 1.6
    shaft = floor & (xx > dx0 + (yy - dy1) * 0.4 - 10) & (xx < dx1 + spread)
    fall = np.exp(-(yy - dy1) / 260)
    shaft_f = np.asarray(Image.fromarray((shaft * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(18)), float) / 255
    base += (shaft_f * fall * 30)[..., None] * np.array([1.0, 0.92, 0.78])
    halo = np.exp(-(((xx - 290) / 140) ** 2 + ((yy - 225) / 170) ** 2))
    base += (halo * 10)[..., None] * np.array([1.0, 0.94, 0.82])
    L = frame_lines([(dx0 - 8, dy0 - 8, dx1 + 8, dy1)], 110)
    base += (L / 255)[..., None] * np.array([34, 32, 28])
    return finish(base, grain=2.5, vignette=0.45)


def chamber():
    base = np.full((H, W, 3), 8.0)
    vx, vy = 640, 330
    glow = np.exp(-(((xx - vx) / 160) ** 2 + ((yy - vy - 40) / 120) ** 2))
    base += glow[..., None] * np.array([18, 18, 22])
    im = Image.new("L", (W, H), 0); d = ImageDraw.Draw(im)
    for k in range(10):                                                    # arches, one behind another
        t = 0.32 * 1.33 ** k; w_ = 180 * t; h_ = 230 * t; top = vy - h_; bot = vy + 120 * t
        a_ = int(min(150, 60 + 12 * k))
        d.arc([vx - w_, top, vx + w_, top + 2 * w_], 180, 360, fill=a_, width=2)
        d.line([(vx - w_, top + w_), (vx - w_, bot)], fill=a_, width=2)
        d.line([(vx + w_, top + w_), (vx + w_, bot)], fill=a_, width=2)
    L = np.asarray(im.filter(ImageFilter.GaussianBlur(1.0)), float)
    base += (L / 255)[..., None] * np.array([34, 33, 38])
    return finish(base, grain=2.5, vignette=0.55)


def cave():
    n1 = fractal(140, 5, seed=11); n2 = fractal(60, 4, seed=12)
    cx, cy = 760, 300                                                      # the mouth, slightly right
    r = np.sqrt(((xx - cx) / 520) ** 2 + ((yy - cy) / 300) ** 2)
    edge = r + (n1 - 0.5) * 0.55 + (n2 - 0.5) * 0.2                       # a rough, layered silhouette
    opening = np.clip((1.0 - edge) * 3.0, 0, 1)
    rock = 1 - opening
    base = np.full((H, W, 3), 7.0)
    base += (opening * 13)[..., None] * np.array([0.85, 0.9, 1.0])        # hazy depth beyond
    strata = 0.5 + 0.5 * np.sin((yy + n1 * 160) / 9.0)                    # faint strata in the rock
    base += (rock * strata * 6 * (0.5 + n2))[..., None] * np.array([1.0, 0.95, 0.88])
    lip = np.exp(-((edge - 1.0) / 0.06) ** 2)                             # light catching the rim, soft
    base += (lip * 8)[..., None] * np.array([1.0, 0.96, 0.9])
    return finish(base, grain=2.8, vignette=0.4)


def backrooms():
    base = np.full((H, W, 3), 9.0) * np.array([1.06, 1.0, 0.72])          # the yellow cast
    stripes = 0.5 + 0.5 * np.sin(xx * 2 * math.pi / 22)                   # wallpaper
    wall = (yy > 150) & (yy < 470)
    base += (stripes * wall * 4)[..., None] * np.array([1.1, 1.0, 0.6])
    vx, vy = 640, 150; segs = []
    for i in range(-14, 15):                                              # ceiling grid in perspective
        x = vx + i * 34; segs.append((x, vy, vx + (x - vx) * 7, -900))
    for k in range(1, 12):
        y = vy - 4 * k ** 1.6
        if y > 0: segs.append((0, y, W, y))
    L = lines_layer(segs, 120, width=2, blur=0.8)
    ceil = yy < vy
    base += (L / 255 * ceil)[..., None] * np.array([30, 28, 18])
    for (px, py, pw, ph) in ((470, 60, 120, 18), (690, 60, 120, 18), (560, 112, 70, 9), (690, 112, 70, 9)):   # dim panels
        g = np.exp(-(((xx - px - pw / 2) / (pw * 0.8)) ** 2 + ((yy - py) / (ph * 1.6)) ** 2))
        base += (g * 18)[..., None] * np.array([1.0, 1.0, 0.75])
    floor_dark = np.clip((yy - 470) / 160, 0, 1)
    base *= (1 - 0.35 * floor_dark)[..., None]
    return finish(base, grain=3.0, vignette=0.5)


def great_hall():
    base = np.full((H, W, 3), 8.0)
    vx, vy = 640, 290
    fog = np.exp(-(((xx - vx) / 420) ** 2 + ((yy - vy) / 160) ** 2))
    base += (fog * 16)[..., None] * np.array([0.92, 0.95, 1.0])
    for side in (-1, 1):                                                   # columns marching into the fog
        for k in range(12):
            t = 0.9 ** k; x = vx + side * (90 + 520 * t); w_ = 70 * t
            top, bot = vy - 330 * t, vy + 240 * t
            col = (xx > x - w_ / 2) & (xx < x + w_ / 2) & (yy > top) & (yy < bot)
            shade = 0.55 + 0.45 * (1 - t)                                 # nearer columns darker (silhouettes)
            base[col] = base[col] * (1 - 0.6 * (1 - shade)) + 4
            edge = (np.abs(xx - (x - side * w_ / 2)) < max(1.0, 2.2 * t)) & (yy > top) & (yy < bot)
            base[edge] += 10 * t + 4
    return finish(base, grain=2.6, vignette=0.55)


def threshold_remix():
    """Deep Hall's receding doorways, the farthest one open: its light spills back along the floor through every
    frame (Open Door's shaft), catching each frame's edge where it crosses, with dust in the beam"""
    vx, vy = 470, 250
    base = np.full((H, W, 3), 8.0)
    dw, dh = 26, 44                                                        # the open door, far away
    door = (np.abs(xx - vx) < dw) & (np.abs(yy - vy) < dh)
    base[door] += np.array([34, 30, 24])
    halo = np.exp(-(((xx - vx) / 90) ** 2 + ((yy - vy) / 120) ** 2))
    base += (halo * 16)[..., None] * np.array([1.0, 0.92, 0.78])
    floor_y = vy + dh                                                      # the shaft: from the door's sill, widening
    poly = Image.new("L", (W, H), 0); dp = ImageDraw.Draw(poly)
    dp.polygon([(vx - dw, floor_y), (vx + dw, floor_y), (vx + dw + (H - floor_y) * 2.1, H), (vx - dw + (H - floor_y) * 0.55, H)], fill=255)
    shaft = np.asarray(poly.filter(ImageFilter.GaussianBlur(16)), float) / 255
    fall = np.exp(-(yy - floor_y).clip(0) / 300) * (yy >= floor_y - 4)
    base += (shaft * fall * 26)[..., None] * np.array([1.0, 0.92, 0.78])
    rects = []
    for k in range(1, 9):                                                  # doorway after doorway
        t = 1.42 ** k
        rects.append((vx - dw * 1.15 * t, vy - dh * 1.15 * t, vx + dw * 1.15 * t, vy + dh * 1.15 * t))
    L = frame_lines(rects, 150)
    near = np.clip(np.sqrt((xx - vx) ** 2 + (yy - vy) ** 2) / 600, 0.35, 1)
    lit = 1 + 2.2 * shaft * fall                                           # edges catch the light where it crosses
    base += (L / 255 * near * lit)[..., None] * np.array([38, 35, 31])
    dust = np.zeros((H, W))                                                # dust in the beam
    g = np.random.default_rng(77)
    for _ in range(900):
        x, y = int(g.uniform(0, W)), int(g.uniform(floor_y, H))
        dust[y, x] = g.uniform(0.4, 1.0)
    dust = np.asarray(Image.fromarray((dust * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)), float) / 255
    base += (dust * shaft * fall * 40)[..., None] * np.array([1.0, 0.94, 0.82])
    return finish(base, grain=2.5, vignette=0.45)


def threshold_air():
    """the final page background: Deep Hall's soft doorway in its nested frames, in the clear bottom-right corner,
    its light spilling along the floor; airy sonics: faint ripples spreading from the door like a reverb tail,
    drifting haze, dust motes"""
    vx, vy = 1150, 440                                                    # page coordinates (the corner left open)
    base = np.full((H, W, 3), 8.0)
    glow = np.exp(-(((xx - vx) / 60) ** 2 + ((yy - vy) / 85) ** 2))       # Deep Hall's warm, soft door
    base += glow[..., None] * np.array([46, 39, 30])
    halo = np.exp(-(((xx - vx) / 230) ** 2 + ((yy - vy) / 200) ** 2))
    base += (halo * 9)[..., None] * np.array([1.0, 0.92, 0.8])
    rects = []
    for k in range(7):                                                     # doorway after doorway, receding
        t = 0.5 * 1.45 ** k
        rects.append((vx - 60 * t, vy - 100 * t, vx + 60 * t, vy + 100 * t))
    L = frame_lines(rects, 140)
    near = np.clip(1.25 - np.sqrt((xx - vx) ** 2 + (yy - vy) ** 2) / 900, 0.15, 1)   # frames fade away from the door
    base += (L / 255 * near)[..., None] * np.array([38, 35, 31])
    floor_y = vy + 50                                                      # the light along the floor, to the left
    poly = Image.new("L", (W, H), 0); dp = ImageDraw.Draw(poly)
    dp.polygon([(vx - 30, floor_y), (vx + 30, floor_y), (vx - 120, H), (vx - 30 - (H - floor_y) * 3.2, H)], fill=255)
    shaft = np.asarray(poly.filter(ImageFilter.GaussianBlur(20)), float) / 255
    base += (shaft * np.exp(-(yy - floor_y).clip(0) / 240) * 20)[..., None] * np.array([1.0, 0.92, 0.78])
    r = np.sqrt((xx - vx) ** 2 + ((yy - vy) * 1.15) ** 2)                 # ripples: a reverb tail, made visible
    wob = (fractal(90, 3, seed=21) - 0.5) * 14
    phase = (r + wob) / 34.0
    rings = np.exp(-((phase - np.round(phase)) / 0.07) ** 2) * np.exp(-r / 520) * (r > 70)
    base += (rings * 9)[..., None] * np.array([0.92, 0.95, 1.0])
    haze = fractal(220, 4, seed=22)                                         # drifting haze, stretched sideways
    haze = np.asarray(Image.fromarray((haze * 255).astype(np.uint8)).resize((W // 4, H)).resize((W, H), Image.BICUBIC), float) / 255
    base += (np.clip(haze - 0.5, 0, 1) * 18)[..., None] * np.array([0.9, 0.93, 1.0])
    gg = np.random.default_rng(91); motes = np.zeros((H, W))               # dust motes, more near the light
    for _ in range(1400):
        x, y = gg.uniform(0, W), gg.uniform(0, H)
        p_ = 0.25 + 0.75 * np.exp(-((x - vx) ** 2 + (y - vy) ** 2) / (2 * 380 ** 2))
        if gg.random() < p_: motes[int(y), int(x)] = gg.uniform(0.3, 1.0)
    motes = np.asarray(Image.fromarray((motes * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8)), float) / 255
    base += (motes * 26)[..., None] * np.array([1.0, 0.95, 0.85])
    return finish(base, grain=2.5, vignette=0.45)


def threshold_air():
    """the page's background: Deep Hall's soft doorway (a glow inside receding frames) in the empty lower right, its
    light spilling down-left along the floor; over it, air: drifting haze wisps, dust motes in the light, and faint
    rings spreading from the doorway like sound through air"""
    vx, vy = 1120, 470                                                    # the doorway (page coordinates)
    base = np.full((H, W, 3), 8.0)
    glow = np.exp(-(((xx - vx) / 46) ** 2 + ((yy - vy) / 70) ** 2))       # the far light, soft (Deep Hall)
    base += glow[..., None] * np.array([44, 37, 28])
    halo = np.exp(-(((xx - vx) / 190) ** 2 + ((yy - vy) / 220) ** 2))
    base += (halo * 12)[..., None] * np.array([1.0, 0.92, 0.78])
    rects = []
    for k in range(10):                                                    # doorway after doorway
        t = 0.5 * 1.42 ** k
        rects.append((vx - 48 * t, vy - 80 * t, vx + 48 * t, vy + 80 * t))
    L = frame_lines(rects, 140)
    near = np.clip(np.sqrt((xx - vx) ** 2 + (yy - vy) ** 2) / 520, 0.3, 1)
    floor_y = vy + 40                                                      # the light along the floor, down-left
    poly = Image.new("L", (W, H), 0); dp = ImageDraw.Draw(poly)
    dp.polygon([(vx - 34, floor_y), (vx + 34, floor_y), (vx + 34 - (H - floor_y) * 0.2, H), (vx - 34 - (H - floor_y) * 3.4, H)], fill=255)
    shaft = np.asarray(poly.filter(ImageFilter.GaussianBlur(20)), float) / 255
    fall = np.exp(-(yy - floor_y).clip(0) / 420) * (yy >= floor_y - 6)
    base += (shaft * fall * 34)[..., None] * np.array([1.0, 0.92, 0.78])
    lit = 1 + 2.0 * shaft * fall
    base += (L / 255 * near * lit)[..., None] * np.array([36, 34, 30])
    # air: wisps (low-frequency noise stretched sideways), brighter towards the light
    w = fractal(170, 4, seed=31)
    w = np.asarray(Image.fromarray((w * 255).astype(np.uint8)).resize((W // 6, H)).resize((W, H), Image.BICUBIC), float) / 255
    wisps = np.clip((w - 0.55) * 3.0, 0, 1) * (0.35 + 0.65 * halo)
    base += (wisps * 13)[..., None] * np.array([0.9, 0.95, 1.0])
    # ripples: faint rings from the doorway, spreading wider and fading
    r = np.sqrt((xx - vx) ** 2 + ((yy - vy) * 1.15) ** 2)
    rings = 0.5 + 0.5 * np.cos(2 * math.pi * np.sqrt(r) / 3.1)
    ring_env = np.exp(-r / 520) * (r > 60)
    ring_env = np.exp(-r / 300) * (r > 70)
    base += (np.clip(rings - 0.93, 0, 1) * 14 * ring_env * 3.2)[..., None] * np.array([0.92, 0.95, 1.0])   # felt, not seen
    # dust motes: more, and brighter, in the light
    g = np.random.default_rng(91); dust = np.zeros((H, W))
    for _ in range(4200):
        x, y = int(g.uniform(0, W)), int(g.uniform(0, H))
        dust[y, x] = g.uniform(0.3, 1.0)
    dust = np.asarray(Image.fromarray((dust * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6)), float) / 255
    base += (dust * (0.18 + 2.6 * np.maximum(halo, shaft * fall)) * 36)[..., None] * np.array([1.0, 0.95, 0.85])
    return finish(base, grain=2.4, vignette=0.4)


def threshold_air():
    """The final background: Deep Hall's soft glowing doorway at the end of receding frames, the light spilling back
    along the floor (the remix), and an airy layer: drifting wisps along a smooth flow field, slow haze banks, and
    faint wavefronts leaving the doorway like sound travelling out of the room. Dust in the beam."""
    vx, vy = 1092, 452                                                    # the empty lower right: the far end of the room
    base = np.full((H, W, 3), 8.0)
    haze = fractal(220, 4, seed=31)                                       # slow haze banks
    base += ((haze - 0.5) * 11 + 2)[..., None] * np.array([0.92, 0.95, 1.0])
    core = np.exp(-(((xx - vx) / 46) ** 2 + ((yy - vy) / 68) ** 2))      # Deep Hall's door: a soft glow, no hard edge
    halo = np.exp(-(((xx - vx) / 120) ** 2 + ((yy - vy) / 150) ** 2))
    base += (core * 34 + halo * 12)[..., None] * np.array([1.0, 0.9, 0.74])
    dh = 60
    floor_y = vy + dh * 0.75                                             # the beam along the floor, down and right
    poly = Image.new("L", (W, H), 0); dp = ImageDraw.Draw(poly)
    dp.polygon([(vx - 30, floor_y), (vx + 30, floor_y), (vx + 30 - (H - floor_y) * 0.45, H), (vx - 30 - (H - floor_y) * 1.9, H)], fill=255)   # spilling down and left, towards the controls
    shaft = np.asarray(poly.filter(ImageFilter.GaussianBlur(22)), float) / 255
    fall = np.exp(-np.clip(yy - floor_y, 0, None) / 320) * (yy >= floor_y - 6)
    base += (shaft * fall * 20)[..., None] * np.array([1.0, 0.92, 0.78])
    rects = []
    for k in range(1, 9):                                                  # doorway after doorway
        t = 1.40 ** k
        rects.append((vx - 30 * t, vy - 50 * t, vx + 30 * t, vy + 50 * t))
    L = frame_lines(rects, 140)
    near = np.clip(np.sqrt((xx - vx) ** 2 + (yy - vy) ** 2) / 600, 0.3, 1)
    base += (L / 255 * near * (1 + 2 * shaft * fall))[..., None] * np.array([36, 33, 29])
    rings = Image.new("L", (W, H), 0); dr = ImageDraw.Draw(rings)        # wavefronts leaving the doorway
    for k in range(1, 14):
        r = 70 * 1.26 ** k; a = int(max(0, 150 - 8 * k))
        dr.rounded_rectangle([vx - r * 1.25, vy - r, vx + r * 1.25, vy + r], radius=int(r * 0.9), outline=a, width=2)
    R = np.asarray(rings.filter(ImageFilter.GaussianBlur(1.6)), float)
    base += (R / 255 * 40)[..., None] * np.array([0.9, 0.95, 1.0])
    wisp = Image.new("L", (W, H), 0); dw_ = ImageDraw.Draw(wisp)          # drifting air: streamlines of a smooth flow field
    fx = fractal(300, 3, seed=41); fy = fractal(300, 3, seed=42)
    g = np.random.default_rng(5)
    for _ in range(95):
        x, y = g.uniform(0, W), g.uniform(0, H); pts = [(x, y)]
        for _ in range(int(g.uniform(80, 200))):
            i, j = int(min(H - 1, max(0, y))), int(min(W - 1, max(0, x)))
            ang = (fx[i, j] - 0.5) * 4.0 + (fy[i, j] - 0.5) * 2.0 - 0.25   # mostly drifting right and slightly up
            x += 3.2 * math.cos(ang); y += 3.2 * math.sin(ang); pts.append((x, y))
        dw_.line(pts, fill=int(g.uniform(60, 140)), width=2)
    Wl = np.asarray(wisp.filter(ImageFilter.GaussianBlur(1.3)), float)
    base += (Wl / 255 * 34)[..., None] * np.array([0.85, 0.93, 1.0])     # a little cooler than the light: air
    dust = np.zeros((H, W)); g2 = np.random.default_rng(78)
    for _ in range(1100):
        x, y = int(g2.uniform(0, W)), int(g2.uniform(0, H)); dust[y, x] = g2.uniform(0.3, 1.0)
    dust = np.asarray(Image.fromarray((dust * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)), float) / 255
    base += (dust * (0.15 + shaft * fall + halo) * 34)[..., None] * np.array([1.0, 0.94, 0.84])
    return finish(base, grain=2.5, vignette=0.45)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (("threshold", threshold), ("fluorescent", fluorescent), ("interference", interference), ("poolrooms", poolrooms),
                     ("threshold_deep", threshold_deep), ("threshold_open", threshold_open), ("chamber", chamber),
                     ("cave", cave), ("backrooms", backrooms), ("great_hall", great_hall),
                     ("threshold_remix", threshold_remix), ("threshold_air", threshold_air)):
        fn().save(os.path.join(OUT, "bg_%s.png" % name))
        print("bg_%s.png" % name)
    import shutil                                                          # the page's background: threshold_air
    shutil.copy(os.path.join(OUT, "bg_threshold_air.png"), os.path.join(OUT, "texture.png"))
    print("texture.png = bg_threshold_air.png")
