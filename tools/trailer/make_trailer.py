#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_trailer.py wide|tall <out.mp4> [--frames t1,t2,...] -- the Liminal Hz trailer.

wide: 1920x1080, the whole score (~63 s). tall: 1080x1920, a ~31 s cut of the same score at bar boundaries (Reels,
TikTok, Shorts). The music is tools/trailer/mix.wav (render.sh, mix.py: synthesized, every part played through Liminal
Hz). The pages are the plugin's real page (tools/demos/render_page.py) in the states the demos recorded
(tools/demos/render_audio.sh), the paintings are the plugin's own IR art (vst/art/bg_*.png).
--frames: those moments (seconds of the video) as PNGs instead of a video."""
import json, os, subprocess, sys, wave
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "demos")); sys.path.insert(0, HERE)
import render_page, score

FPS, SR = 30, 44100
BAR, BEAT = score.BAR, score.BEAT
FONTS = render_page.FONTS
ART = os.path.join(ROOT, "vst", "art")
DEMOS = os.path.join(ROOT, "vst", "build", "demos")
MIX = os.path.join(ROOT, "vst", "build", "trailer", "mix.wav")
IV, GR, AMB, DIM = (236, 230, 218), (160, 154, 144), (222, 186, 120), (96, 92, 86)
IMPACTS = [score.t_of(6), score.t_of(18), score.t_of(23)]
DROP_KICKS = [score.t_of(b, k) for b in range(18, 23) for k in (0, 0.75, 1.5, 2.5, 3.25)]

def F(style, size): return ImageFont.truetype(FONTS + {"r": "TitilliumWeb-Regular.ttf", "s": "TitilliumWeb-SemiBold.ttf",
                                                         "b": "TitilliumWeb-Bold.ttf"}[style], max(8, int(size)))
def ease(x): x = min(1.0, max(0.0, x)); return x * x * (3 - 2 * x)

# ---- assets ---------------------------------------------------------------------------------------------------------
def painting(name, gain=3.2):
    im = Image.open(os.path.join(ART, "bg_%s.png" % name)).convert("RGB")
    im = ImageEnhance.Brightness(im).enhance(gain)
    return ImageEnhance.Contrast(im).enhance(1.25)

REGIONS = {  # page areas (1280x628 page pixels), from the built skin's TUI.json
    "longir": (1030, 500, 250, 100), "time": (1022, 252, 252, 245), "duck": (790, 434, 140, 199),
    "sync": (144, 426, 192, 207), "preset": (8, 60, 360, 64), "list": (6, 116, 420, 312), "shape": (430, 0, 850, 250),
    "knobs": (0, 420, 1040, 208)}

class Pages:
    """the page in each recorded state, at 9 glow positions (and the PRESET list open)"""
    def __init__(self):
        self.cache = {}
    def state(self, name):
        return json.load(open(os.path.join(DEMOS, name + ".json")))
    def get(self, name, glow=0, open_list=False):
        key = (name, glow, open_list)
        if key not in self.cache:
            st = self.state(name)
            idx = {p["name"]: p["i"] for p in st["params"]}
            opts = {idx["Playhead"]: glow}
            if open_list: opts[idx["Preset List"]] = 1
            st["options"] = opts
            self.cache[key] = render_page.render(st)
        return self.cache[key]
    def at(self, name, t, open_list=False):
        """the glow moving as it did when the demo played (the state's playhead), looped"""
        ph = self.state_ph(name)
        k = ph[int(t * FPS) % len(ph)] if ph else 0
        return self.get(name, int(round(k / 3.0)) * 3, open_list)
    _ph = {}
    def state_ph(self, name):
        if name not in self._ph:
            ph = self.state(name).get("playhead", [])
            ph = [p for p in ph if p > 0] or [0]
            self._ph[name] = ph
        return self._ph[name]

# ---- drawing --------------------------------------------------------------------------------------------------------
class Canvas:
    def __init__(self, W, H):
        self.W, self.H = W, H
        self.tall = H > W
        y, x = np.mgrid[0:H, 0:W]
        r = np.sqrt(((x - W / 2) / (W / 2)) ** 2 + ((y - H / 2) / (H / 2)) ** 2)
        self.vignette = np.clip(1.15 - 0.55 * r ** 1.6, 0.25, 1.0)[:, :, None].astype(np.float32)
        g = np.random.default_rng(3)
        self.grain = [g.normal(0, 3, (H // 2, W // 2, 1)).astype(np.float32) for _ in range(4)]   # (light: it costs bits)

    def cover(self, im, zoom=1.0, cx=0.5, cy=0.5):
        """im scaled to cover the frame, zoomed about (cx, cy)"""
        s = max(self.W / im.width, self.H / im.height) * zoom
        w, h = im.width * s, im.height * s
        x0 = (w - self.W) * cx; y0 = (h - self.H) * cy
        box = (x0 / s, y0 / s, (x0 + self.W) / s, (y0 + self.H) / s)
        return im.transform((self.W, self.H), Image.EXTENT, box, Image.BILINEAR)

    def page(self, frame, pg, t_frac=0.0, region=None, zoom_to=0.0, width_frac=None, y_frac=None, alpha=1.0, mark=None):
        """the page, scaled into the frame; zoom_to (0..1) moves the view toward region; mark draws an amber box"""
        wf = width_frac or (0.98 if self.tall else 0.80)
        full = (0, 0, 1280, 628)
        if region and zoom_to > 0:
            rx, ry, rw, rh = region
            pad = 0.35
            aspect = 1280 / 628
            cw = min(1280, max(rw * (1 + 2 * pad), rh * (1 + 2 * pad) * aspect)); ch = cw / aspect   # (never past the page)
            cx, cy = rx + rw / 2, ry + rh / 2
            tgt = (max(0, min(1280 - cw, cx - cw / 2)), max(0, min(628 - ch, cy - ch / 2)), cw, ch)
            z = ease(zoom_to)
            view = tuple(full[i] + (tgt[i] - full[i]) * z for i in range(2)) + tuple(full[i + 2] + (tgt[i + 2] - full[i + 2]) * z for i in range(2))
        else:
            view = full
        ow = int(self.W * wf); oh = int(ow * 628 / 1280)
        vx, vy, vw, vh = view
        im = pg.transform((ow, oh), Image.EXTENT, (vx, vy, vx + vw, vy + vh), Image.BILINEAR)
        if mark:
            d = ImageDraw.Draw(im)
            rx, ry, rw, rh = mark
            sx, sy = ow / vw, oh / vh
            d.rounded_rectangle(((rx - vx) * sx, (ry - vy) * sy, (rx + rw - vx) * sx, (ry + rh - vy) * sy), 12, outline=AMB, width=4)
        x = (self.W - ow) // 2; y = int(self.H * (y_frac if y_frac is not None else (0.42 if self.tall else 0.5)) - oh / 2)
        bd = ImageDraw.Draw(frame)
        if alpha >= 1:
            bd.rectangle((x - 3, y - 3, x + ow + 2, y + oh + 2), outline=(70, 66, 60), width=2)
            frame.paste(im, (x, y))
        else:
            region_im = frame.crop((x, y, x + ow, y + oh))
            frame.paste(Image.blend(region_im, im, alpha), (x, y))
        return (x, y, ow, oh)

    def text(self, frame, s, y, size, color=IV, alpha=1.0, style="s", track=0.0, x=None, anchor="mm"):
        if alpha <= 0.01 or not s: return
        f = F(style, size)
        layer = Image.new("RGBA", frame.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
        if track:
            widths = [d.textlength(ch, font=f) for ch in s]
            total = sum(widths) + track * size * (len(s) - 1)
            cx = x if x is not None else self.W / 2
            px = cx - total / 2 if anchor[0] == "m" else cx
            for ch, w in zip(s, widths):
                d.text((px, y), ch, font=f, fill=color + (int(255 * alpha),), anchor="l" + anchor[1]); px += w + track * size
        else:
            d.text((x if x is not None else self.W / 2, y), s, font=f, fill=color + (int(255 * alpha),), anchor=anchor)
        frame.paste(Image.alpha_composite(frame.convert("RGBA"), layer).convert("RGB"))

    def fit(self, s, size, style="s", maxw=None, track=0.0):
        """a size at which s fits the frame's width"""
        maxw = maxw or self.W * 0.9
        d = ImageDraw.Draw(Image.new("RGB", (8, 8)))
        while size > 20:
            f = F(style, size)
            w = d.textlength(s, font=f) + track * size * (len(s) - 1)
            if w <= maxw: break
            size -= 4
        return size

    def wordmark(self, frame, y, size, alpha=1.0, by=True):
        f, fh = F("r", size), F("s", size)
        layer = Image.new("RGBA", frame.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
        a = int(255 * alpha)
        widths = [d.textlength(ch, font=f) for ch in "LIMINAL"]
        total = sum(widths) + size * 0.24 * 6 + size * 0.3 + d.textlength("Hz", font=fh)
        x = (self.W - total) / 2
        for ch, w in zip("LIMINAL", widths):
            d.text((x, y), ch, font=f, fill=(222, 214, 200, a), anchor="ls"); x += w + size * 0.24
        x += size * 0.3 - size * 0.24
        d.text((x, y), "Hz", font=fh, fill=IV + (a,), anchor="ls")
        if by: d.text((self.W / 2, y + size * 0.62), "by Gm0rb", font=F("r", size * 0.3), fill=GR + (a,), anchor="ms")
        frame.paste(Image.alpha_composite(frame.convert("RGBA"), layer).convert("RGB"))

    def finish(self, frame, t, flash=0.0, shake=0.0, letterbox=True, fade=1.0):
        a = np.asarray(frame, dtype=np.float32)
        if shake > 0.5:
            r = np.random.default_rng(int(t * 1000))
            dx, dy = (r.uniform(-1, 1, 2) * shake).astype(int)
            a = np.roll(np.roll(a, dy, 0), dx, 1)
        a = a * self.vignette
        g = self.grain[int(t * FPS / 6) % len(self.grain)]          # (a new grain every 0.2 s)
        a = a + np.repeat(np.repeat(g, 2, 0), 2, 1)[: self.H, : self.W]
        if flash > 0: a = a + (255 - a) * flash
        a = a * fade
        if letterbox and not self.tall:
            bar = int(self.H * 0.06); a[:bar] = 0; a[-bar:] = 0
        return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))

# ---- the scenes (on the score's clock) ------------------------------------------------------------------------------
PAINT = {k: painting(k) for k in ("threshold_deep", "backrooms", "fluorescent", "poolrooms", "cave", "great_hall",
                                  "interference", "chamber", "threshold_open", "threshold_air")}
FLICKER = ["fluorescent", "poolrooms", "cave", "great_hall", "interference", "chamber", "backrooms", "threshold_air"]
FEATURES = [  # (bar, title, subtitle, state, region, open list?)
    (8, "19 FACTORY IRs", "spaces · echoes · strange rooms · textures", "stabs", None, True),
    (9, "YOUR OWN IRs", "any .wav · or pick from TONE3000 on your phone", "LIST", None, False),
    (10, "UP TO 20 SECONDS", "LONG IR: the whole tail, on Gen 1 and Gen 2", "pad", "longir", False),
    (11, "DECAY · STRETCH · REVERSE", "reshape any IR without leaving the page", "hits", "time", False),
    (12, "DUCK", "the reverb gets out of your groove's way", "duck80", "duck", False),
    (13, "TEMPO SYNC", "pre-delay locked to your MPC's tempo", "sync_on", "sync", False),
]
TEXTURES = [("STRUCK PIPE", "pipe"), ("GRAIN CLOUD", "grain"), ("TUNED CHORD", "chord"), ("BREATH", "breath"), ("BOUNCING BALL", "ball")]
DROP = [("SPACE: EMPTY MALL", "drums"), ("ECHO: DREAM PING-PONG", "keys"), ("STRANGE: REVERSE BLOOM", "hits"),
        ("SPACE: BACKROOMS", "pad"), ("STRANGE: LIMINAL HZ", "stabs"), ("TEXTURE: TUNED CHORD", "chord"),
        ("ECHO: PARKING SLAP", "sync_on"), ("TEXTURE: GRAIN CLOUD", "grain"), ("TEXTURE: STRUCK PIPE", "pipe"),
        ("TEXTURE: BOUNCING BALL", "ball")]
END = score.t_of(23) + 7.8

class Trailer:
    def __init__(self, mode):
        self.mode = mode
        self.C = Canvas(1920, 1080) if mode == "wide" else Canvas(1080, 1920)
        self.P = Pages()
        self.list_page = Image.open(os.path.join(ROOT, "docs", "images", "ir-list.png")).convert("RGB")

    def bg(self, name, t, t0, dur, zoom=(1.0, 1.12), dim=1.0):
        im = self.C.cover(PAINT[name], zoom[0] + (zoom[1] - zoom[0]) * min(1, (t - t0) / dur), 0.5, 0.55)
        return ImageEnhance.Brightness(im).enhance(dim) if dim != 1.0 else im

    def page_img(self, name, t, open_list=False):
        return self.list_page if name == "LIST" else self.P.at(name, t, open_list)

    def big(self, fr, s, y, size, **kw):
        C = self.C; size = C.fit(s, size, kw.get("style", "s"), track=kw.get("track", 0.0)); C.text(fr, s, y, size, **kw)

    def frame(self, t):
        C, W, H = self.C, self.C.W, self.C.H
        tall = C.tall
        flash = sum(max(0.0, 0.85 * np.exp(-(t - i) / 0.12)) for i in IMPACTS if t >= i)
        shake = sum(28 * np.exp(-(t - i) / 0.35) for i in IMPACTS if t >= i)
        shake += sum(9 * np.exp(-(t - k) / 0.08) for k in DROP_KICKS if 0 <= t - k < 0.4)
        fade = 1.0
        b = t / BAR
        if t < score.t_of(2):                                         # every room has a sound
            fr = self.bg("threshold_deep", t, 0, 2 * BAR)
            fade = ease(t / 1.6)
            a = ease((t - 0.8) / 0.5) * (1 - ease((t - 4.3) / 0.4))
            self.big(fr, "EVERY ROOM HAS A SOUND.", H * 0.5, 84 if not tall else 76, alpha=a, track=0.08, style="r")
        elif t < score.t_of(4):                                       # some rooms shouldn't exist
            fr = self.bg("backrooms", t, score.t_of(2), 2 * BAR, (1.05, 1.2))
            t0 = score.t_of(2)
            a = ease((t - t0 - 0.5) / 0.5) * (1 - ease((t - t0 - 4.4) / 0.3))
            self.big(fr, "SOME ROOMS", H * (0.45 if tall else 0.45), 96, alpha=a, track=0.08, style="r")
            self.big(fr, "SHOULDN'T EXIST.", H * (0.53 if tall else 0.57), 96, alpha=a, track=0.08, style="s", color=AMB)
        elif t < score.t_of(6) - BEAT * 0.5:                          # the flicker
            t0 = score.t_of(4)
            step = BEAT if t < score.t_of(5) else BEAT / 2
            k = int((t - t0) / step)
            fr = self.bg(FLICKER[k % len(FLICKER)], t, t0, 2 * BAR, (1.1, 1.3), dim=0.7 + 0.5 * (((t - t0) / step) % 1 < 0.25))
            if t < score.t_of(5): self.big(fr, "AKAI MPC  ·  FORCE", H * 0.5, 64, alpha=ease((t - t0 - 0.3) / 0.3), track=0.25, style="r")
            else: self.big(fr, "GEN 1  ·  GEN 2", H * 0.5, 64, track=0.25, style="r")
        elif t < score.t_of(6):                                       # silence
            fr = Image.new("RGB", (W, H), (0, 0, 0))
        elif t < score.t_of(8):                                       # the title
            t0 = score.t_of(6)
            fr = self.bg("threshold_deep", t, t0, 2 * BAR, (1.25, 1.1), dim=0.55)
            C.wordmark(fr, H * (0.47 if tall else 0.5), (150 if not tall else 118) * (1.06 - 0.06 * ease((t - t0) / 1.2)))
            self.big(fr, "CONVOLUTION REVERB FOR YOUR MPC", H * (0.6 if tall else 0.66), 44 if not tall else 40,
                     alpha=ease((t - t0 - 1.0) / 0.5), track=0.2, style="r", color=GR)
        elif t < score.t_of(14):                                      # the features, one a bar
            bar = int(b); _, title, sub, st, reg, open_list = FEATURES[bar - 8]
            t0 = score.t_of(bar); u = (t - t0) / BAR
            fr = self.bg(["great_hall", "chamber", "cave", "interference", "fluorescent", "poolrooms"][bar - 8], t, t0, BAR, (1.1, 1.18), dim=0.45)
            pg = self.page_img(st, t, open_list)
            if tall:
                C.page(fr, pg, region=REGIONS.get(reg), zoom_to=(u - 0.2) * 1.6 if reg else 0, y_frac=0.56,
                       mark=REGIONS.get(reg) if reg and u > 0.35 else None)
                self.big(fr, title, H * 0.25, 104, color=AMB if bar >= 12 else IV, track=0.05)
                self.big(fr, sub, H * 0.31, 40, style="r", color=GR)
            else:
                C.page(fr, pg, region=REGIONS.get(reg), zoom_to=(u - 0.2) * 1.6 if reg else 0, width_frac=0.62, y_frac=0.57,
                       mark=REGIONS.get(reg) if reg and u > 0.35 else None)
                self.big(fr, title, H * 0.16, 92, color=AMB if bar >= 12 else IV, track=0.05)
                self.big(fr, sub, H * 0.235, 36, style="r", color=GR)
            fade = 0.35 + 0.65 * ease(u / 0.06)                       # a dip on each cut
        elif t < score.t_of(16):                                      # Textures
            t0 = score.t_of(14); u = (t - t0) / (2 * BAR)
            fr = self.bg("interference", t, t0, 2 * BAR, (1.0, 1.15), dim=0.55)
            C.page(fr, self.page_img("chord", t), alpha=0.35, width_frac=0.9 if not tall else 1.0, y_frac=0.5)
            self.big(fr, "TEXTURES", H * 0.44, 170 if not tall else 150, track=0.12, alpha=ease(u / 0.1))
            self.big(fr, "IRs THAT AREN'T ROOMS", H * (0.56 if not tall else 0.53), 52, style="r", color=AMB, track=0.15, alpha=ease((u - 0.25) / 0.1))
            self.big(fr, "your drums, through a struck pipe · a grain cloud · a chord", H * (0.63 if not tall else 0.58),
                     34, style="r", color=GR, alpha=ease((u - 0.5) / 0.1))
        elif t < score.t_of(18) - BEAT * 0.5:                         # the riser: the five textures, faster
            t0 = score.t_of(16); dur = 2 * BAR - BEAT * 0.5
            k = min(4, int((t - t0) / (dur / 5))); name, st = TEXTURES[k]
            u = (t - t0) / dur
            fr = self.bg(FLICKER[k], t, t0, dur, (1.0, 1.4), dim=0.5)
            C.page(fr, self.page_img(st, t), region=REGIONS["shape"], zoom_to=0.4 + 0.6 * u, y_frac=0.55 if not tall else 0.55)
            self.big(fr, name, H * (0.16 if not tall else 0.22), 110 + 40 * u, track=0.08, color=AMB)
        elif t < score.t_of(18):
            fr = Image.new("RGB", (W, H), (0, 0, 0))
        elif t < score.t_of(23):                                      # the drop: a cut every beat
            t0 = score.t_of(18); k = int((t - t0) / BEAT); bar = int(b)
            name, st = DROP[k % len(DROP)]
            crops = [None, "shape", "knobs", "list", None]
            fr = self.bg(FLICKER[k % len(FLICKER)], t, t0 + k * BEAT, BEAT, (1.15, 1.25), dim=0.55)
            if bar == 20:                                             # the presets, as MPC's own menu names them
                u = (t - score.t_of(20)) / BAR
                self.big(fr, "ALSO IN MPC'S OWN", H * (0.2 if not tall else 0.24), 58, style="r", track=0.15)
                self.big(fr, "PRESET MENU", H * (0.29 if not tall else 0.3), 96, color=AMB, track=0.06)
                names = ["Space: Empty Mall", "Echo: Dream Ping-Pong", "Strange: Glass Void", "Strange: Liminal Hz",
                         "Texture: Tuned Chord", "Texture: Bouncing Ball"]
                for j, nm in enumerate(names):
                    C.text(fr, nm, H * ((0.42 if not tall else 0.4) + j * (0.075 if not tall else 0.05)), 46 if not tall else 50,
                           color=IV if j != 3 else AMB, style="s" if j == 3 else "r", alpha=ease((u - j * 0.08) / 0.1))
            elif bar == 22:
                C.wordmark(fr, H * 0.42, 120 if not tall else 100, by=False)
                self.big(fr, "FREE.", H * 0.62, 220 if not tall else 200, color=AMB, track=0.1)
            else:
                r = crops[k % len(crops)]
                C.page(fr, self.page_img(st, t), region=REGIONS.get(r), zoom_to=1.0 if r else 0, y_frac=0.56 if not tall else 0.55)
                self.big(fr, name, H * (0.14 if not tall else 0.22), 80, track=0.06, color=IV if k % 2 else AMB)
        else:                                                         # the end card
            t0 = score.t_of(23)
            fr = self.bg("threshold_deep", t, t0, 8, (1.15, 1.0), dim=0.4)
            C.wordmark(fr, H * (0.4 if tall else 0.38), 160 if not tall else 120)
            self.big(fr, "Free convolution reverb for Akai MPC & Force", H * (0.5 if tall else 0.54), 50, style="s",
                     alpha=ease((t - t0 - 0.6) / 0.5))
            self.big(fr, "Gen 1 & Gen 2  ·  19 factory IRs  ·  your own IRs  ·  LONG IR  ·  Duck  ·  Tempo SYNC", H * (0.55 if tall else 0.6),
                     34, style="r", color=GR, alpha=ease((t - t0 - 1.0) / 0.5))
            self.big(fr, "github.com/gmorb/mpc-vst-liminal-hz", H * (0.63 if tall else 0.7), 46, style="s", color=AMB,
                     alpha=ease((t - t0 - 1.6) / 0.5))
            fade = 1 - ease((t - (END - 1.6)) / 1.6)
        return C.finish(fr, t, min(1.0, flash), shake, fade=fade)

# ---- the cuts --------------------------------------------------------------------------------------------------------
def segments(mode):
    """(source start, source end) on the score's clock, joined in order"""
    if mode == "wide": return [(0.0, END)]
    T = score.t_of
    return [(T(4), T(6)), (T(6), T(8)), (T(12), T(14)), (T(16), T(18)), (T(18), T(21)), (T(22), T(23)), (T(23), END)]

def read_mix():
    w = wave.open(MIX); x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float32) / 32768
    return x

def main():
    mode, out = sys.argv[1], sys.argv[2]
    T = Trailer(mode); segs = segments(mode)
    total = sum(b - a for a, b in segs)
    def src(t):                                                       # video time -> score time
        for a, b in segs:
            if t < b - a: return a + t
            t -= b - a
        return segs[-1][1]
    if len(sys.argv) > 3 and sys.argv[3] == "--frames":
        for t in [float(x) for x in sys.argv[4].split(",")]:
            T.frame(src(t)).save(out.replace(".mp4", "_%05.2f.png" % t))
        print("%s: %.1f s" % (mode, total)); return
    x = read_mix(); parts = []; xf = int(0.012 * SR)
    for a, b in segs:                                                 # the audio cut the same way (12 ms crossfades)
        seg = x[int(a * SR): int(b * SR)].copy()
        if parts:
            seg[:xf] *= np.linspace(0, 1, xf)[:, None]; parts[-1][-xf:] *= np.linspace(1, 0, xf)[:, None]
        parts.append(seg)
    audio = np.concatenate(parts)
    fade = int(1.2 * SR); audio[-fade:] *= np.linspace(1, 0, fade)[:, None]
    tmpa, tmpv, tmp = out + ".wav", out + ".video.mp4", out + ".part.mp4"
    with wave.open(tmpa, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes((np.clip(audio, -1, 1) * 32767).astype("<i2").tobytes())
    W, H = T.C.W, T.C.H; n = int(round(total * FPS))
    p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS),
                          "-i", "-", "-c:v", "libx264", "-preset", "medium", "-crf", "21", "-pix_fmt", "yuv420p", "-movflags", "+faststart", tmpv],
                         stdin=subprocess.PIPE)
    for i in range(n):
        p.stdin.write(T.frame(src(i / FPS)).tobytes())
    p.stdin.close(); assert p.wait() == 0
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmpv, "-i", tmpa, "-map", "0:v", "-map", "1:a", "-c:v", "copy",
                        "-c:a", "aac", "-b:a", "256k", "-shortest", "-movflags", "+faststart", tmp])
    assert r.returncode == 0
    os.replace(tmp, out); os.remove(tmpv); os.remove(tmpa)
    print("wrote %s: %.1f s, %d frames" % (out, total, n))

if __name__ == "__main__":
    main()
