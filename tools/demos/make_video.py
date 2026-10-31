#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_video.py <demo> <out.mp4> [--frames t1,t2,...] -- one vertical demo video (1080x1920, 30 fps, H.264 + AAC):
the dry phrase, then the same phrase through Liminal Hz (the plugin's real output, demo_host), with the page as the
plugin reported it (render_page.py) and the glow moving with the audio. --frames writes those moments as PNGs instead.
Run tools/demos/render_audio.sh first: it makes the audio and the plugin's page state in vst/build/demos."""
import json, os, subprocess, sys, wave
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageEnhance, ImageFilter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_page

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(render_page.ROOT, "vst/art") + "/"
AUDIO = os.path.join(render_page.ROOT, "vst/build/demos")    # render_audio.sh writes here
FONTS = render_page.FONTS
W, H, FPS, SR = 1080, 1920, 30, 44100
IV, GR, DIM, AMB = (232, 226, 214), (154, 149, 140), (110, 105, 98), (217, 183, 122)

DEMOS = {
    "pad": dict(src="pad", title=["Your MPC,", "in the Backrooms"],
                settings="Backrooms · Decay 300% · Feedback 40% · LONG IR", dry_end=5.5),
    "drums": dict(src="drums", title=["Dry drums,", "into an empty mall"],
                  settings="Factory preset: Empty Mall", dry_end=5.6),
    "hits": dict(src="hits", title=["Reverse reverb,", "live on your MPC"],
                 settings="Factory preset: Reverse Bloom · Mix 55%", dry_end=6.3),
}

def F(name, size): return ImageFont.truetype(FONTS + name, size)

def read_wav(p):
    with open(p, "rb") as f: d = f.read()
    pos, data = 12, None
    while pos + 8 <= len(d):
        tag, sz = d[pos:pos + 4], int.from_bytes(d[pos + 4:pos + 8], "little")
        if tag == b"data": data = np.frombuffer(d[pos + 8:pos + 8 + sz], dtype="<f4").reshape(-1, 2)
        pos += 8 + sz + (sz & 1)
    return data.astype(np.float64)

def write_wav16(p, x):
    y = np.clip(x, -1, 1); y = (y * 32767).astype("<i2")
    with wave.open(p, "wb") as w: w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(y.tobytes())

def build(name):
    D = DEMOS[name]
    dry = read_wav(os.path.join(AUDIO, D["src"] + ".wav"))
    wet = read_wav(os.path.join(AUDIO, D["src"] + "_wet.wav"))
    state = json.load(open(os.path.join(AUDIO, D["src"] + ".json")))
    gap = int(0.35 * SR)
    dry_part = dry[: int(D["dry_end"] * SR)]
    audio = np.concatenate([dry_part, np.zeros((gap, 2)), wet])
    audio *= 10 ** (-1 / 20) / max(1e-9, np.max(np.abs(audio)))          # peak -1 dBFS
    t_wet = (len(dry_part) + gap) / SR
    total = len(audio) / SR
    return D, audio, state, t_wet, total

def background():
    bg = Image.new("RGB", (W, H), (10, 11, 13))
    tex = Image.open(ART + "texture.png").convert("RGB")
    s = H / tex.height; tex = tex.resize((int(tex.width * s), H), Image.LANCZOS)
    tex = tex.crop(((tex.width - W) // 2, 0, (tex.width - W) // 2 + W, H))
    bg = Image.blend(bg, tex, 0.55)
    glow = Image.new("RGB", (W, H), (0, 0, 0)); g = ImageDraw.Draw(glow)
    g.ellipse((W * 0.55, H * 0.70, W * 1.25, H * 1.05), fill=(70, 58, 40))           # the doorway's warm light
    glow = glow.filter(ImageFilter.GaussianBlur(160))
    return Image.fromarray(np.clip(np.asarray(bg, dtype=np.int16) + np.asarray(glow, dtype=np.int16), 0, 255).astype(np.uint8))

def wordmark(d, x, y, size):
    f, fh = F("TitilliumWeb-Regular.ttf", size), F("TitilliumWeb-SemiBold.ttf", size)
    for ch in "LIMINAL":
        d.text((x, y), ch, font=f, fill=(214, 206, 192), anchor="ls"); x += d.textlength(ch, font=f) + size * 0.22
    x += size * 0.2
    d.text((x, y), "Hz", font=fh, fill=IV, anchor="ls"); x += d.textlength("Hz", font=fh)
    d.text((x + size * 0.25, y), "by Gm0rb", font=F("TitilliumWeb-Regular.ttf", int(size * 0.42)), fill=GR, anchor="ls")

def envelope(audio, n):
    m = np.max(np.abs(audio), axis=1); edges = np.linspace(0, len(m), n + 1).astype(int)
    return np.array([m[a:b].max() if b > a else 0 for a, b in zip(edges[:-1], edges[1:])])

class Frames:
    def __init__(self, name):
        self.D, self.audio, self.state, self.t_wet, self.total = build(name)
        self.base = background()
        d = ImageDraw.Draw(self.base)
        wordmark(d, 70, 190, 64)
        ft = F("TitilliumWeb-SemiBold.ttf", 86)
        for k, line in enumerate(self.D["title"]): d.text((70, 345 + k * 104), line, font=ft, fill=IV, anchor="ls")
        d.text((70, 1838), "Free · Akai MPC & Force · Gen 1 & 2", font=F("TitilliumWeb-Regular.ttf", 36), fill=GR, anchor="ls")
        # the page: dry (dimmed, no glow) and wet (25 glow positions)
        P = dict(self.state)
        self.page_w = 1000; self.page_h = round(628 * self.page_w / 1280); self.page_xy = (40, 600)
        def page(opt):
            P["options"] = opt
            im = render_page.render(P).resize((self.page_w, self.page_h), Image.LANCZOS)
            return im
        ph_index = next(p["i"] for p in self.state["params"] if p["name"] == "Playhead")
        self.wet_pages = [page({ph_index: k}) for k in range(25)]
        self.dry_page = ImageEnhance.Brightness(page({ph_index: 0})).enhance(0.42)
        self.ph = self.state["playhead"]
        self.env = envelope(self.audio, 920)
        self.end_card = self.make_end_card()

    def make_end_card(self):
        im = Image.new("RGB", (W, H), (8, 9, 10)); d = ImageDraw.Draw(im)
        wordmark(d, 90, 860, 92)
        d.text((92, 980), "Free convolution reverb", font=F("TitilliumWeb-SemiBold.ttf", 54), fill=IV, anchor="ls")
        d.text((92, 1048), "for Akai MPC & Force", font=F("TitilliumWeb-SemiBold.ttf", 54), fill=IV, anchor="ls")
        d.text((92, 1140), "14 factory spaces · your own IRs · TONE3000", font=F("TitilliumWeb-Regular.ttf", 38), fill=GR, anchor="ls")
        d.text((92, 1260), "github.com/gmorb/mpc-vst-liminal-hz", font=F("TitilliumWeb-SemiBold.ttf", 40), fill=AMB, anchor="ls")
        return im

    def frame(self, t):
        im = self.base.copy(); d = ImageDraw.Draw(im)
        wet = t >= self.t_wet
        if wet:
            k = min(len(self.ph) - 1, int((t - self.t_wet) * FPS)); page = self.wet_pages[max(0, min(24, self.ph[k]))]
        else:
            page = self.dry_page
        x0, y0 = self.page_xy
        d.rounded_rectangle((x0 - 6, y0 - 6, x0 + self.page_w + 5, y0 + self.page_h + 5), 10, outline=(52, 50, 47), width=2)
        im.paste(page, (x0, y0))
        # the state: DRY, or LIMINAL Hz with its settings
        y = y0 + self.page_h + 80
        if wet:
            d.rounded_rectangle((70, y - 40, 470, y + 28), 34, fill=(58, 46, 26), outline=AMB, width=2)
            d.text((270, y - 6), "LIMINAL Hz ON", font=F("TitilliumWeb-SemiBold.ttf", 40), fill=AMB, anchor="mm")
            d.text((70, y + 92), self.D["settings"], font=F("TitilliumWeb-Regular.ttf", 36), fill=GR, anchor="ls")
            d.text((70, y + 146), "Level matched to the dry sound with the Output knob", font=F("TitilliumWeb-Regular.ttf", 30), fill=DIM, anchor="ls")
        else:
            d.rounded_rectangle((70, y - 40, 270, y + 28), 34, fill=(26, 27, 30), outline=(90, 87, 82), width=2)
            d.text((170, y - 6), "DRY", font=F("TitilliumWeb-SemiBold.ttf", 40), fill=IV, anchor="mm")
            d.text((70, y + 92), "No reverb: the sound as it is", font=F("TitilliumWeb-Regular.ttf", 36), fill=GR, anchor="ls")
        # the clip's waveform with a cursor (dry grey, Liminal Hz amber)
        wx, wy, ww, wh = 80, 1560, 920, 150
        cut = self.t_wet / self.total
        for i, v in enumerate(self.env):
            hh = max(1, v ** 0.6 * wh / 2); col = AMB if i / len(self.env) >= cut else (120, 116, 108)
            if i / len(self.env) > t / self.total: col = tuple(int(c * 0.45) for c in col)
            d.line((wx + i, wy - hh, wx + i, wy + hh), fill=col)
        cx = wx + ww * min(1, t / self.total); d.line((cx, wy - wh / 2 - 12, cx, wy + wh / 2 + 12), fill=IV, width=3)
        # the end card fades in over the tail's last 2.6 s
        end_start = self.total - 2.6
        if t >= end_start:
            a = min(1.0, (t - end_start) / 0.45); im = Image.blend(im, self.end_card, a)
        return im

def main():
    name, out = sys.argv[1], sys.argv[2]
    V = Frames(name)
    if len(sys.argv) > 3 and sys.argv[3] == "--frames":
        for t in [float(x) for x in sys.argv[4].split(",")]:
            V.frame(t).save(out.replace(".mp4", "_%05.2f.png" % t))
        print("total %.2f s, wet from %.2f s" % (V.total, V.t_wet)); return
    tmpv, tmpa, tmp = out + ".video.mp4", out + ".wav", out + ".part.mp4"
    n = int(round(V.total * FPS))
    p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS),
                          "-i", "-", "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", tmpv],
                         stdin=subprocess.PIPE)
    for i in range(n): p.stdin.write(V.frame(i / FPS).tobytes())
    p.stdin.close(); assert p.wait() == 0
    write_wav16(tmpa, V.audio)
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmpv, "-i", tmpa, "-map", "0:v", "-map", "1:a", "-c:v", "copy",
                        "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", tmp])
    assert r.returncode == 0
    os.replace(tmp, out); os.remove(tmpv); os.remove(tmpa)
    print("wrote %s: %.1f s, %d frames" % (out, V.total, n))

if __name__ == "__main__":
    main()
