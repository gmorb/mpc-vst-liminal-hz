#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_posters.py -- the README's demo thumbnails (docs/demos/liminal-hz-demo-<name>.jpg, 540x960): a frame from each
video's Liminal Hz part, with a play button on its waveform. Run after make_video.py."""
import os, subprocess, tempfile
from PIL import Image, ImageDraw
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
D = os.path.join(ROOT, "docs", "demos")
for name, t in (("pad", 8.0), ("drums", 8.5), ("hits", 9.2)):
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "f.png")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(t), "-i", os.path.join(D, "liminal-hz-demo-%s.mp4" % name),
                        "-frames:v", "1", png], check=True)
        im = Image.open(png).convert("RGB").resize((540, 960), Image.LANCZOS)
    ov = Image.new("RGBA", im.size, (0, 0, 0, 0)); d = ImageDraw.Draw(ov)
    cx, cy, r = 270, 782, 58
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(14, 13, 12, 210), outline=(217, 183, 122, 255), width=4)
    d.polygon([(cx - 19, cy - 30), (cx - 19, cy + 30), (cx + 32, cy)], fill=(217, 183, 122, 255))
    Image.alpha_composite(im.convert("RGBA"), ov).convert("RGB").save(os.path.join(D, "liminal-hz-demo-%s.jpg" % name), quality=88)
    print("poster", name)
