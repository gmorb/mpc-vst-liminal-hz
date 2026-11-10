#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_trailer.py wide|tall <out.mp4> [--style dark|cyber|trap] [--frames t1,t2,...] -- the Liminal Hz trailer.

wide: 1920x1080, the whole score (~63 s). tall: 1080x1920, a ~31 s cut of the same score at bar boundaries (Reels,
TikTok, Shorts). --style picks the score (score.py: dark, 80s synth horror; cyber, cyberpunk) and the
picture's look to match (dark: red titles, a cold-red grade; cyber: the plugin's own amber and ivory, a warm grade,
scanlines, a colour split, and glitched slices on the hits; trap: amber and ivory, hard cuts on the 808s and
claps, a punch-in on every 808). The music is <style>/mix.wav (render.sh, mix.py: synthesized, every part played through Liminal
Hz). The pages are the plugin's real page (tools/demos/render_page.py) in the states the demos recorded
(tools/demos/render_audio.sh), the paintings are the plugin's own IR art (vst/art/bg_*.png).
--frames: those moments (seconds of the video) as PNGs instead of a video."""
import json, os, subprocess, sys, wave
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "demos")); sys.path.insert(0, HERE)
if "--style" in sys.argv: os.environ["TRAILER_STYLE"] = sys.argv[sys.argv.index("--style") + 1]   # (sets the score's tempo)
import render_page, score

FPS, SR = 30, 44100
BAR, BEAT = score.BAR, score.BEAT
FONTS = render_page.FONTS
ART = os.path.join(ROOT, "vst", "art")
DEMOS = os.path.join(ROOT, "vst", "build", "demos")
def _style():
    a = sys.argv
    return a[a.index("--style") + 1] if "--style" in a else "dark"
STYLE = _style()
MIX = os.path.join(ROOT, "vst", "build", "trailer", STYLE, "mix.wav")
IV, GR, AMB, DIM = (236, 230, 218), (160, 154, 144), (222, 186, 120), (96, 92, 86)
if STYLE == "dark": AMB = (214, 52, 44)                         # (the accent: Carpenter red)
MAG = (246, 226, 188)                                            # (cyber's second accent: the page's warm ivory)
GRADE = {"dark": (np.array([1.06, 0.84, 0.80], np.float32), 0.0), "cyber": (np.array([1.05, 0.98, 0.88], np.float32), 3.0),
         "trap": (np.array([1.06, 0.99, 0.88], np.float32), 1.0)}[STYLE]
if STYLE == "dark":                                               # 5/4: the drop's kicks on 1, 2.5, 4, 5
    IMPACTS = [score.t_of(6), score.t_of(18), score.t_of(23)]
    DROP_KICKS = [score.t_of(b, k) for b in range(18, 23) for k in (0, 1.5, 3, 4)]
elif STYLE == "cyber":                                           # cyber: the drop's kicks on 1, the a of 2, the and of 3
    IMPACTS = [score.t_of(4), score.t_of(20), score.t_of(28)]
    DROP_KICKS = [score.t_of(b, k) for b in range(20, 28) for k in (0, 1.75, 2.5)]
else:                                                             # trap: the cuts land on the 808s and the claps
    IMPACTS = [score.t_of(4), score.t_of(20), score.t_of(28)]
    HITS808, CLAPS = score.trap_hits()
    DROP_KICKS = [t for t in HITS808 if score.t_of(20) <= t < score.t_of(28)]
GLITCHES = IMPACTS + DROP_KICKS + [score.t_of(b, 3 + k * 0.125) for b in (11, 23) for k in range(8)] + \
    [score.t_of(19, 2.5 + k * 0.125) for k in range(8)] + [score.t_of(b, 3.5) for b in range(3, 28, 4)]
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"     # (the HUD's type; Titillium if it isn't there)
def M(size):
    try: return ImageFont.truetype(MONO, max(8, int(size)))
    except OSError: return F("r", size)

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
        yy = (y / H)[:, :, None].astype(np.float32)                    # cyber: a low amber light from above and below
        self.neon = (np.array([34, 24, 8], np.float32) * (1 - yy) ** 2.5 + np.array([40, 22, 6], np.float32) * yy ** 2.2)
        g = np.random.default_rng(3)
        self.grain = [g.normal(0, 3, (H // 2, W // 2, 1)).astype(np.float32) for _ in range(4)]   # (light: it costs bits)

    def cover(self, im, zoom=1.0, cx=0.5, cy=0.5):
        """im scaled to cover the frame, zoomed about (cx, cy)"""
        s = max(self.W / im.width, self.H / im.height) * zoom
        w, h = im.width * s, im.height * s
        x0 = (w - self.W) * cx; y0 = (h - self.H) * cy
        box = (x0 / s, y0 / s, (x0 + self.W) / s, (y0 + self.H) / s)
        return im.transform((self.W, self.H), Image.EXTENT, box, Image.BILINEAR)

    def page(self, frame, pg, t_frac=0.0, region=None, zoom_to=0.0, width_frac=None, y_frac=None, alpha=1.0, mark=None, x_frac=None):
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
        x = (self.W - ow) // 2 if x_frac is None else int(self.W * x_frac - ow / 2); y = int(self.H * (y_frac if y_frac is not None else (0.42 if self.tall else 0.5)) - oh / 2)
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

    def finish(self, frame, t, flash=0.0, shake=0.0, letterbox=True, fade=1.0, glitch=0.0):
        a = np.asarray(frame, dtype=np.float32)
        if STYLE == "cyber":                                            # a colour split, glitched slices
            split = int(3 + 14 * glitch)
            a = a.copy(); a[:, :, 0] = np.roll(a[:, :, 0], split, 1); a[:, :, 2] = np.roll(a[:, :, 2], -split, 1)
            if glitch > 0.15:
                r = np.random.default_rng(int(t * 997))
                for _ in range(int(3 + 6 * glitch)):
                    y0 = int(r.uniform(0, self.H - 20)); h = int(r.uniform(8, 60 + 120 * glitch))
                    a[y0:y0 + h] = np.roll(a[y0:y0 + h], int(r.uniform(-1, 1) * 90 * glitch), 1)
        if shake > 0.5:
            r = np.random.default_rng(int(t * 1000))
            dx, dy = (r.uniform(-1, 1, 2) * shake).astype(int)
            a = np.roll(np.roll(a, dy, 0), dx, 1)
        a = a * self.vignette
        a = a * GRADE[0] + GRADE[1]                                     # the style's grade
        if STYLE == "cyber": a = a + self.neon; a[1::3] *= 0.84        # amber light, scanlines
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
END = score.t_of(23) + 7.8 if STYLE == "dark" else score.t_of(28) + (4.6 if STYLE == "cyber" else 4.5)

class Trailer:
    def __init__(self, mode):
        self.mode = mode
        self.C = Canvas(1920, 1080) if mode == "wide" else Canvas(1080, 1920)
        self.P = Pages()
        self.list_page = Image.open(os.path.join(ROOT, "docs", "images", "ir-list.png")).convert("RGB")

    def bg(self, name, t, t0, dur, zoom=(1.0, 1.12), dim=1.0):
        im = self.C.cover(PAINT[name], zoom[0] + (zoom[1] - zoom[0]) * min(1, (t - t0) / dur), 0.5, 0.55)
        dim *= 1.25 if STYLE == "cyber" else 1.0                        # (cyber: a little more light)
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
                self.big(fr, name, H * (0.14 if not tall else 0.22), 80, track=0.06, color=(MAG if STYLE == "cyber" else IV) if k % 2 else AMB)
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
        glitch = max([np.exp(-(t - g) / 0.07) for g in GLITCHES if 0 <= t - g < 0.3] + [0.0])
        return C.finish(fr, t, min(1.0, flash), shake, fade=fade, glitch=min(1.0, glitch + flash))

# ---- the cuts --------------------------------------------------------------------------------------------------------
class CyberTrailer(Trailer):
    """cyber's own edit: a terminal boot, a HUD title over a moving amber grid, spec panels, callouts, oscilloscope
    traces of the Textures, a countdown, beat-synced punch-ins on the drop"""
    BOOT = ["> boot liminal_hz.vst", "> host: akai mpc / force   gen 1 + gen 2", "> factory IRs .......... 19 ok",
            "> long_ir .............. 20 s ok", "> duck ................. ok", "> sync ................. 100 bpm ok",
            "> textures ............. 5 ok", "> ready_"]
    TEX = [("STRUCK PIPE", "Struck_Pipe"), ("GRAIN CLOUD", "Grain_Cloud"), ("TUNED CHORD", "Tuned_Chord"),
           ("BREATH", "Breath"), ("BOUNCING BALL", "Bouncing_Ball")]
    def __init__(self, mode):
        super().__init__(mode)
        import wave as _w
        self.traces = []
        for _, fn in self.TEX:                                    # the IRs' own waveforms, for the scope
            w = _w.open(os.path.join(ROOT, "packaging", "factory", "Textures", fn + ".wav"))
            a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8).reshape(-1, 6)
            v = ((a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8) | (a[:, 2].astype(np.int32) << 16)) << 8 >> 8) / 8388608.0
            self.traces.append(v / max(1e-9, np.abs(v).max()))

    def grid(self, fr, t, bright=1.0):
        """an amber floor grid moving toward the camera, and a horizon glow"""
        W, H = self.C.W, self.C.H; d = ImageDraw.Draw(fr)
        hy = int(H * (0.58 if not self.C.tall else 0.62)); cx = W / 2
        for k in range(12):                                       # horizontal lines (perspective, moving)
            z = ((k + (t * 1.2) % 1.0) / 12.0)
            y = hy + (H - hy) * z ** 2.2
            c = tuple(int(v * bright * (0.25 + 0.75 * z)) for v in MAG)
            d.line((0, y, W, y), fill=c, width=1 + int(2 * z))
        for k in range(-14, 15):                                  # rays to the vanishing point
            x1 = cx + k * W / 9
            d.line((cx + k * 6, hy, x1, H), fill=tuple(int(v * bright * 0.45) for v in MAG), width=1)
        d.line((0, hy, W, hy), fill=tuple(int(v * bright) for v in AMB), width=2)

    def bracket(self, fr, box, col=None, L=26, w=3):
        d = ImageDraw.Draw(fr); x0, y0, x1, y1 = box; col = col or AMB
        for (px, py, dx, dy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
            d.line((px, py, px + dx * L, py), fill=col, width=w); d.line((px, py, px, py + dy * L), fill=col, width=w)

    def mono(self, fr, s, xy, size, col=None, alpha=1.0, anchor="la"):
        layer = Image.new("RGBA", fr.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
        d.text(xy, s, font=M(size), fill=(col or AMB) + (int(255 * alpha),), anchor=anchor)
        fr.paste(Image.alpha_composite(fr.convert("RGBA"), layer).convert("RGB"))

    def panel(self, fr, pg, region, box, label, t, slide=1.0):
        """a page crop in a HUD panel, sliding in"""
        x0, y0, x1, y1 = box; w, h = x1 - x0, y1 - y0
        rx, ry, rw, rh = region
        asp = w / h
        if rw / rh > asp: ch = rw / asp; cy = ry + rh / 2; view = (rx, max(0, min(628 - ch, cy - ch / 2)), rw, min(628, ch))
        else: cw = rh * asp; cx = rx + rw / 2; view = (max(0, min(1280 - cw, cx - cw / 2)), ry, min(1280, cw), rh)
        vx, vy, vw, vh = view
        im = pg.transform((w, h), Image.EXTENT, (vx, vy, vx + vw, vy + vh), Image.BILINEAR)
        off = int((1 - ease(slide)) * self.C.W * 0.6)
        fr.paste(im, (x0 + off, y0))
        self.bracket(fr, (x0 + off - 6, y0 - 6, x1 + off + 6, y1 + 6))
        self.mono(fr, label, (x0 + off, y0 - 40), 28 if not self.C.tall else 30)

    def frame(self, t):
        C, W, H = self.C, self.C.W, self.C.H
        tall = C.tall; T = score.t_of
        flash = sum(max(0.0, 0.8 * np.exp(-(t - i) / 0.1)) for i in IMPACTS if t >= i)
        shake = sum(22 * np.exp(-(t - i) / 0.3) for i in IMPACTS if t >= i) + sum(10 * np.exp(-(t - k) / 0.07) for k in DROP_KICKS if 0 <= t - k < 0.3)
        fade = 1.0
        fr = Image.new("RGB", (W, H), (6, 6, 12))
        if t < T(4):                                              # boot
            self.grid(fr, t, 0.25 + 0.5 * t / T(4))
            n_chars = int((t - 0.3) * 38)
            y = H * (0.16 if not tall else 0.2); size = 44 if not tall else 36
            for ln in self.BOOT:
                if n_chars <= 0: break
                shown = ln[:n_chars]; n_chars -= len(ln) + 6
                cur = "_" if n_chars < 0 and int(t * 4) % 2 else ""
                self.mono(fr, shown + cur, (W * 0.08, y), size, IV if "ok" not in shown else AMB)
                y += size * 1.6
            if t > T(3, 3): fade = max(0.0, 1 - (t - T(3, 3)) / (BEAT * 0.9))
        elif t < T(8):                                            # the title, HUD
            u = (t - T(4)) / (4 * BAR)
            self.grid(fr, t, 1.0)
            C.wordmark(fr, H * (0.4 if not tall else 0.42), (150 if not tall else 112) * (1.08 - 0.08 * ease(u * 4)))
            self.mono(fr, "CONVOLUTION REVERB // AKAI MPC + FORCE", (W / 2, H * (0.52 if not tall else 0.5)), 36 if not tall else 28,
                      MAG, ease((t - T(4) - 0.6) / 0.4), "mm")
            self.bracket(fr, (W * 0.18, H * 0.24, W * 0.82, H * 0.56) if not tall else (W * 0.06, H * 0.32, W * 0.94, H * 0.53))
        elif t < T(12):                                           # spec panels, one a bar
            self.grid(fr, t, 0.6)
            k = int((t - T(8)) / BAR)
            specs = [("stabs", "shape", "> ir_shape"), ("stabs", "preset", "> 19 factory IRs"),
                     ("pad", "longir", "> long_ir: 20 s"), ("LIST", "list", "> your IRs + TONE3000")]
            if not tall: boxes = [(120, 170, 900, 500), (1020, 170, 1800, 500), (120, 620, 900, 950), (1020, 620, 1800, 950)]
            else: boxes = [(60, 260, 1020, 640), (60, 740, 1020, 1120), (60, 1220, 1020, 1600), (60, 1700, 1020, 1880)]
            for j in range(k + 1):
                st, reg, lab = specs[j]
                pg = self.list_page if st == "LIST" else self.P.at(st, t)
                self.panel(fr, pg, REGIONS[reg], boxes[j], lab, t, (t - T(8 + j)) / (BEAT * 1.2))
        elif t < T(16):                                           # callouts, one a bar
            k = int((t - T(12)) / BAR); u = (t - T(12 + k)) / BAR
            items = [("DUCK", "the reverb ducks under your hits", "duck80", "duck"),
                     ("TEMPO SYNC", "pre-delay locked to the grid", "sync_on", "sync"),
                     ("LONG IR", "twenty seconds of tail", "pad", "longir"),
                     ("PRESET MENU", "all 19 in MPC's own menu", "stabs", "preset")]
            word, sub, st, reg = items[k]
            self.grid(fr, t, 0.5)
            if not tall:
                self.big(fr, word, H * 0.36, 120, color=AMB if k % 2 == 0 else MAG, track=0.04, x=W * 0.27)
                self.mono(fr, "> " + sub, (W * 0.27, H * 0.46), 34, IV, ease(u / 0.15), "mm")
                self.panel(fr, self.P.at(st, t), REGIONS[reg], (int(W * 0.55), int(H * 0.22), int(W * 0.93), int(H * 0.72)), "", t, u / 0.12)
            else:
                self.big(fr, word, H * 0.2, 130, color=AMB if k % 2 == 0 else MAG, track=0.04)
                self.mono(fr, "> " + sub, (W / 2, H * 0.26), 30, IV, ease(u / 0.15), "mm")
                self.panel(fr, self.P.at(st, t), REGIONS[reg], (60, int(H * 0.33), W - 60, int(H * 0.6)), "", t, u / 0.12)
        elif t < T(20) - BEAT * 0.5:                              # textures on the scope, then the countdown
            self.grid(fr, t, 0.35)
            if t < T(19):
                k = min(4, int((t - T(16)) / (3 * BAR / 5)))
                name, _ = self.TEX[k]; tr = self.traces[k]
                d = ImageDraw.Draw(fr)
                x0, x1 = W * 0.08, W * 0.92; yc = H * 0.5; amp = H * (0.18 if not tall else 0.12)
                n = int(x1 - x0); idx = np.linspace(0, min(len(tr) - 1, int(1.6 * SR)), n).astype(int)
                pts = [(x0 + i, yc - tr[j] * amp) for i, j in enumerate(idx)]
                reveal = int(n * ease((t - T(16) - k * 3 * BAR / 5) / 0.6))
                if reveal > 1: d.line(pts[:reveal], fill=AMB, width=3)
                self.mono(fr, "> textures // irs that aren't rooms", (W * 0.08, H * 0.22), 28, MAG)
                self.big(fr, name, H * 0.76, 110 if not tall else 96, color=IV, track=0.06)
            else:
                n = 3 - int((t - T(19)) / BEAT)
                if n >= 1: self.big(fr, str(n), H * 0.5, 420 if not tall else 520, color=AMB if n != 2 else MAG, style="b")
        elif t < T(20):
            pass
        elif t < T(28):                                           # the drop: a punch-in on every kick
            k = int((t - T(20)) / BEAT); u = (t - T(20) - k * BEAT) / BEAT; bar = int((t - T(20)) / BAR) + 20
            names = ["SPACE: EMPTY MALL", "ECHO: DREAM PING-PONG", "STRANGE: REVERSE BLOOM", "TEXTURE: TUNED CHORD",
                     "STRANGE: LIMINAL HZ", "ECHO: PARKING SLAP", "TEXTURE: GRAIN CLOUD", "SPACE: BACKROOMS"]
            states = ["drums", "keys", "hits", "chord", "stabs", "sync_on", "grain", "pad"]   # (each the page of that preset)
            regs = [None, "shape", "knobs", "list", None, "shape", "knobs", None]
            if bar in (24, 25):
                self.grid(fr, t, 1.0)
                self.big(fr, "FREE." if bar == 24 else "GEN 1 // GEN 2", H * 0.45, 230 if bar == 24 else 120,
                         color=AMB if bar == 24 else MAG, style="b", track=0.05)
                self.mono(fr, "> no subscription. no account. on your mpc." if bar == 24 else "> armv7 + aarch64. one page.",
                          (W / 2, H * 0.6), 30, IV, 1.0, "mm")
            else:
                j = k % len(names)
                bg = self.bg(FLICKER[j % len(FLICKER)], t, T(20) + k * BEAT, BEAT, (1.25, 1.1), dim=0.6)
                fr.paste(bg)
                self.grid(fr, t, 0.6)
                z = 1.0 - 0.25 * ease(u * 3)                          # the punch: in on the kick, settling
                C.page(fr, self.P.at(states[j], t), region=REGIONS.get(regs[j]), zoom_to=1.0 if regs[j] else 0,
                       width_frac=(0.72 if not tall else 0.98) / z, y_frac=0.56)
                self.mono(fr, "> load " + names[j].lower(), (W / 2, H * (0.12 if not tall else 0.2)), 40 if not tall else 34, AMB, 1.0, "mm")
        else:                                                     # end
            self.grid(fr, t, 0.8)
            C.wordmark(fr, H * (0.38 if not tall else 0.4), 150 if not tall else 112)
            self.mono(fr, "free convolution reverb // akai mpc + force", (W / 2, H * 0.5), 32 if not tall else 26, IV,
                      ease((t - T(28) - 0.4) / 0.4), "mm")
            self.mono(fr, "github.com/gmorb/mpc-vst-liminal-hz", (W / 2, H * 0.58), 36 if not tall else 30, AMB,
                      ease((t - T(28) - 1.0) / 0.4), "mm")
            self.bracket(fr, (W * 0.12, H * 0.25, W * 0.88, H * 0.64) if not tall else (W * 0.04, H * 0.3, W * 0.96, H * 0.63))
            fade = 1 - ease((t - (END - 1.4)) / 1.4)
        glitch = max([np.exp(-(t - g) / 0.07) for g in GLITCHES if 0 <= t - g < 0.3] + [0.0])
        return C.finish(fr, t, min(1.0, flash), shake, fade=fade, glitch=min(1.0, glitch + flash))

class TrapTrailer(Trailer):
    """trap's own edit, in the plugin's amber and ivory: black and a slow reveal, then hard cuts on the 808s and the
    claps: huge stacked words with a ghost word drifting behind, page close-ups punching in, a strobe on each clap"""
    VERSE = [("19", "FACTORY IRs", "stabs", "preset", True), ("YOUR", "OWN IRs", "LIST", "list", False),
             ("20 SECOND", "TAILS", "pad", "longir", False), ("RESHAPE", "ANY IR", "hits", "time", False)]
    FEAT = [("DUCK", "the reverb gets out of your 808's way", "duck80", "duck"),
            ("SYNC", "pre-delay in note values, at your tempo", "sync_on", "sync"),
            ("TEXTURES", "five IRs that aren't rooms", None, "shape"),
            ("PRESETS", "all 19 in MPC's own preset menu", "stabs", None)]
    TEX = [("STRUCK PIPE", "pipe"), ("GRAIN CLOUD", "grain"), ("TUNED CHORD", "chord"), ("BREATH", "breath"), ("BOUNCING BALL", "ball")]

    def ghost(self, fr, word, t, t0, y):
        """a huge word, faint, drifting left behind everything"""
        C = self.C; size = int(C.H * (0.42 if not C.tall else 0.2))
        C.text(fr, word, y, size, color=AMB, alpha=0.07, style="b", x=C.W * 0.62 - (t - t0) * 70, anchor="mm")

    def stack(self, fr, a, b, u, x=None):
        """two lines, stacked: a in amber, b in ivory, the second landing a beat later"""
        C, W, H = self.C, self.C.W, self.C.H
        if not C.tall:
            x = x if x is not None else W * 0.28
            sa = C.fit(a, 150, "b", maxw=W * 0.46); sb = C.fit(b, 110, "b", maxw=W * 0.46)
            C.text(fr, a, H * 0.42, sa, color=AMB, style="b", x=x)
            C.text(fr, b, H * 0.42 + sa * 0.95, sb, color=IV, style="b", x=x, alpha=ease((u - 0.25) / 0.06))
        else:
            sa = C.fit(a, 170, "b", maxw=W * 0.9); sb = C.fit(b, 120, "b", maxw=W * 0.9)
            C.text(fr, a, H * 0.17, sa, color=AMB, style="b")
            C.text(fr, b, H * 0.17 + sa * 0.95, sb, color=IV, style="b", alpha=ease((u - 0.25) / 0.06))

    def frame(self, t):
        C, W, H = self.C, self.C.W, self.C.H
        tall = C.tall; T = score.t_of
        flash = sum(max(0.0, 0.8 * np.exp(-(t - i) / 0.1)) for i in IMPACTS if t >= i) + \
            sum(0.16 * np.exp(-(t - c) / 0.05) for c in CLAPS if 0 <= t - c < 0.25)
        shake = sum(20 * np.exp(-(t - i) / 0.3) for i in IMPACTS if t >= i)
        last808 = max([h for h in HITS808 if h <= t] + [-9.0])
        punch = 1 + 0.045 * np.exp(-(t - last808) / 0.09) if T(4) <= t < T(28) else 1.0
        fade = 1.0
        fr = Image.new("RGB", (W, H), (8, 7, 6))
        if t < T(4):                                              # black, a name, the page coming up out of the dark
            if t < T(2, 2):
                C.text(fr, "GM0RB  PRESENTS", H * 0.5, 34 if not tall else 30, color=GR, track=0.4,
                       alpha=ease((t - T(0, 2)) / 0.8) * (1 - ease((t - T(2)) / 0.5)))
            if t > T(2):
                fr.paste(self.bg("threshold_deep", t, T(2), 2 * BAR, (1.0, 1.08), dim=0.45 * ease((t - T(2)) / BAR)))
                C.page(fr, self.P.at("stabs", t), alpha=0.85 * ease((t - T(2, 2)) / BAR), width_frac=(0.6 if not tall else 0.9) + 0.04 * (t - T(2)) / (2 * BAR))
            if t > T(3, 3): fade = max(0.0, 1 - (t - T(3, 3)) / (BEAT * 0.5))
        elif t < T(8):                                            # the title
            fr.paste(self.bg("backrooms", t, T(4), 4 * BAR, (1.05, 1.15), dim=0.55))
            self.ghost(fr, "REVERB", t, T(4), H * 0.5)
            C.wordmark(fr, H * (0.47 if not tall else 0.46), (170 if not tall else 120))
            C.text(fr, "CONVOLUTION REVERB  ·  AKAI MPC + FORCE", H * (0.62 if not tall else 0.56), 34 if not tall else 28,
                   color=IV, track=0.12, alpha=ease((t - T(5)) / 0.5))
        elif t < T(12):                                           # the verse: a cut on each 808, words stacked
            k = int((t - T(8)) / BAR); u = (t - T(8 + k)) / BAR
            a, b, st, reg, ol = self.VERSE[k]
            fr.paste(self.bg(FLICKER[k], t, T(8 + k), BAR, (1.1, 1.18), dim=0.3))
            self.ghost(fr, b.split()[-1], t, T(8 + k), H * 0.5)
            pg = self.list_page if st == "LIST" else self.P.at(st, t, ol)
            close = u < 0.5                                       # close up until the clap, then the whole page
            if not tall:
                C.page(fr, pg, region=REGIONS[reg], zoom_to=1.0 if close else 0.0, width_frac=0.5 * punch, x_frac=0.72,
                       mark=None if close else REGIONS[reg])
            else:
                C.page(fr, pg, region=REGIONS[reg], zoom_to=1.0 if close else 0.0, width_frac=0.96 * punch, y_frac=0.6,
                       mark=None if close else REGIONS[reg])
            self.stack(fr, a, b, u)
        elif t < T(19, 2):                                        # the features, two bars each
            k = min(3, int((t - T(12)) / (2 * BAR))); u = (t - T(12 + 2 * k)) / (2 * BAR)
            word, sub, st, reg = self.FEAT[k]
            fr.paste(self.bg(FLICKER[(k + 4) % 8], t, T(12 + 2 * k), 2 * BAR, (1.05, 1.15), dim=0.3))
            self.ghost(fr, word, t, T(12 + 2 * k), H * 0.5)
            if st is None:                                        # textures: a new one on each beat-and-a-half
                j = min(4, int((t - T(16)) / (2 * BAR / 5))); name, st = self.TEX[j]
                sub = "> " + name.lower()
            pg = self.P.at(st, t, open_list=(word == "PRESETS"))
            z = ease(u * 2.2) if reg else 0.0
            mark = REGIONS[reg] if reg and u > 0.5 else None
            if not tall:
                C.page(fr, pg, region=REGIONS.get(reg), zoom_to=z, width_frac=0.52 * punch, x_frac=0.7, mark=mark)
                C.text(fr, "0%d / 04" % (k + 1), H * 0.3, 30, color=AMB, track=0.3, x=W * 0.24)
                C.text(fr, word, H * 0.44, C.fit(word, 170, "b", maxw=W * 0.4), color=IV, style="b", x=W * 0.24)
                C.text(fr, sub, H * 0.56, 32, color=GR, x=W * 0.24, alpha=ease((u - 0.1) / 0.1))
            else:
                C.page(fr, pg, region=REGIONS.get(reg), zoom_to=z, width_frac=0.96 * punch, y_frac=0.62, mark=mark)
                C.text(fr, "0%d / 04" % (k + 1), H * 0.14, 30, color=AMB, track=0.3)
                C.text(fr, word, H * 0.24, C.fit(word, 190, "b", maxw=W * 0.9), color=IV, style="b")
                C.text(fr, sub, H * 0.32, 32, color=GR, alpha=ease((u - 0.1) / 0.1))
        elif t < T(20) - BEAT * 0.5:                              # three words on the last claps, then black
            words = ["FREE.", "ON YOUR", "MPC."]
            j = min(2, int((t - T(19, 2)) / (BEAT * 0.5)))
            self.big(fr, words[j], H * 0.5, 300 if not tall else 230, color=AMB if j != 1 else IV, style="b")
        elif t < T(20):
            pass
        elif t < T(28):                                           # the drop: a cut on every 808
            k = sum(1 for h in DROP_KICKS if h <= t) - 1
            bar = int((t - T(20)) / BAR) + 20
            if bar in (24, 25):
                fr.paste(self.bg("great_hall" if bar == 24 else "chamber", t, T(bar), BAR, (1.1, 1.2), dim=0.18 if bar == 24 else 0.3))
                self.ghost(fr, "FREE" if bar == 24 else "GEN 2", t, T(bar), H * 0.5)
                self.big(fr, "NO SUBSCRIPTION." if bar == 24 else "GEN 1 + GEN 2", H * 0.47, 190 if not tall else 130, color=AMB, style="b")
                C.text(fr, "no account. on your mpc." if bar == 24 else "armv7 + aarch64. one page.", H * 0.6, 40 if not tall else 34, color=IV, track=0.08)
            else:
                name, st = DROP[k % len(DROP)]; cat, nm = name.split(": ")
                fr.paste(self.bg(FLICKER[k % 8], t, DROP_KICKS[k], BEAT, (1.12, 1.2), dim=0.35))
                self.ghost(fr, cat, t, T(20), H * 0.5)
                C.page(fr, self.P.at(st, t), width_frac=(0.66 if not tall else 0.98) * punch, y_frac=0.58 if not tall else 0.55)
                C.text(fr, cat, H * (0.12 if not tall else 0.2), 34, color=AMB, track=0.4)
                C.text(fr, nm, H * (0.2 if not tall else 0.26), C.fit(nm, 96, "b", maxw=W * 0.9), color=IV, style="b")
        else:                                                     # end
            fr.paste(self.bg("threshold_open", t, T(28), 4.5, (1.0, 1.06), dim=0.4))
            C.wordmark(fr, H * (0.42 if not tall else 0.44), 160 if not tall else 116)
            C.text(fr, "FREE CONVOLUTION REVERB  ·  AKAI MPC + FORCE", H * (0.55 if not tall else 0.53), 32 if not tall else 26,
                   color=IV, track=0.12, alpha=ease((t - T(28) - 0.4) / 0.4))
            C.text(fr, "github.com/gmorb/mpc-vst-liminal-hz", H * (0.63 if not tall else 0.6), 38 if not tall else 32,
                   color=AMB, alpha=ease((t - T(28) - 1.0) / 0.4))
            fade = 1 - ease((t - (END - 1.4)) / 1.4)
        if punch > 1.001 and t >= T(8):                           # the whole frame punches in on the 808
            s2 = (int(W * punch), int(H * punch)); big = fr.resize(s2, Image.BILINEAR)
            fr = big.crop(((s2[0] - W) // 2, (s2[1] - H) // 2, (s2[0] - W) // 2 + W, (s2[1] - H) // 2 + H))
        return C.finish(fr, t, min(1.0, flash), shake, fade=fade)

def segments(mode):
    """(source start, source end) on the score's clock, joined in order"""
    if mode == "wide": return [(0.0, END)]
    T = score.t_of
    if STYLE == "cyber":
        return [(T(2), T(4)), (T(4), T(6)), (T(12), T(16)), (T(18), T(20)), (T(20), T(24)), (T(28), END)]
    if STYLE == "trap":
        return [(T(2), T(4)), (T(4), T(6)), (T(8), T(10)), (T(12), T(18)), (T(18), T(20)), (T(20), T(24)), (T(28), END)]
    return [(T(4), T(6)), (T(6), T(8)), (T(12), T(14)), (T(16), T(18)), (T(18), T(21)), (T(22), T(23)), (T(23), END)]

def read_mix():
    w = wave.open(MIX); x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float32) / 32768
    return x

def main():
    mode, out = sys.argv[1], sys.argv[2]
    T = {"cyber": CyberTrailer, "trap": TrapTrailer}.get(STYLE, Trailer)(mode); segs = segments(mode)
    total = sum(b - a for a, b in segs)
    def src(t):                                                       # video time -> score time
        for a, b in segs:
            if t < b - a: return a + t
            t -= b - a
        return segs[-1][1]
    if "--frames" in sys.argv:
        for t in [float(x) for x in sys.argv[sys.argv.index("--frames") + 1].split(",")]:
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
