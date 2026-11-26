#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_keyboard_art.py -- the on-screen keyboard's pictures (vst/art/): its panel, and each key size at rest and lit.
    python3 tools/make_keyboard_art.py
The page's own materials: graphite keys with a hairline edge, like the arrows and buttons; amber is the page's colour
for something live, so a key lights amber while it takes a tap (and SHIFT stays lit while it's on). SAVE, the one
action that keeps something, has an amber edge at rest. Drawn at 4x and reduced, so the corners are smooth."""
import os
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "vst", "art")
SS = 4


def vgrad(w, h, top, bottom):
    g = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    return g


def key(w, h, face, edge, edge_w=1.0, glow=None, sheen=10):
    """A rounded key, w x h px. face: (top, bottom) colours; edge: rim colour; glow: an amber light inside the rim."""
    W, H, R = w * SS, h * SS, 8 * SS
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=R, fill=255)
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    im.paste(vgrad(W, H, *face), (0, 0), mask)
    if glow:                                       # light from the rim inwards, strongest at the bottom edge
        g = Image.new("L", (W, H), 0)
        ImageDraw.Draw(g).rounded_rectangle((0, 0, W - 1, H - 1), radius=R, outline=255, width=int(7 * SS))
        g = g.filter(ImageFilter.GaussianBlur(7 * SS))
        lay = Image.new("RGBA", (W, H), glow[:3] + (0,))
        lay.putalpha(Image.eval(g, lambda v: int(v * glow[3] / 255)))
        im = Image.alpha_composite(im, Image.composite(lay, Image.new("RGBA", (W, H), (0, 0, 0, 0)), mask))
    d = ImageDraw.Draw(im)
    d.line([(R, int(1.5 * SS)), (W - R, int(1.5 * SS))], fill=(255, 255, 255, sheen), width=SS)   # a lit top edge
    d.rounded_rectangle((0, 0, W - 1, H - 1), radius=R, outline=edge, width=int(edge_w * SS))
    return im.resize((w, h), Image.LANCZOS)


REST = ((31, 32, 36), (21, 22, 25))                # graphite, as the page's buttons
LIT = ((98, 70, 34), (58, 42, 22))                 # warm amber, dark enough that the ivory letter stays clear
EDGE, AMBER, AMBER_HI = (54, 53, 50, 255), (184, 137, 74, 255), (217, 183, 122, 255)

for w in (61, 118, 268):
    key(w, 74, REST, EDGE).save(os.path.join(OUT, "key_%d.png" % w))
    key(w, 74, LIT, AMBER_HI, 2.0, glow=(226, 172, 98, 150), sheen=30).save(os.path.join(OUT, "key_%d_on.png" % w))
key(142, 74, ((48, 44, 38), (33, 31, 28)), AMBER, 1.5).save(os.path.join(OUT, "key_save.png"))
key(142, 74, ((126, 92, 46), (82, 59, 30)), AMBER_HI, 2.0, glow=(240, 200, 130, 170), sheen=40).save(
    os.path.join(OUT, "key_save_on.png"))

# the SHAPE / ROOM / SCAN tabs (88 x 30 tiles): the page shown is the lit one, as the IR browser's FACTORY / MY IRS
key(88, 30, ((22, 23, 26), (18, 19, 22)), (40, 40, 42, 255)).save(os.path.join(OUT, "tab_88.png"))
key(88, 30, ((66, 64, 62), (54, 52, 50)), (96, 92, 86, 255), sheen=18).save(os.path.join(OUT, "tab_88_on.png"))

# SCAN / GRAINS / SNAP tiles beside the tabs (88 x 30): switches, so lit amber when on (the page's colour for live)
key(88, 30, ((22, 23, 26), (18, 19, 22)), (40, 40, 42, 255)).save(os.path.join(OUT, "sw_88.png"))
key(88, 30, LIT, AMBER_HI, 1.5, glow=(226, 172, 98, 120), sheen=24).save(os.path.join(OUT, "sw_88_on.png"))

# the panel the keyboard opens on (the right-hand side, 708 x 620): as the lists' panel, menu_panel.png
pn = Image.new("RGBA", (708 * SS, 620 * SS), (0, 0, 0, 0)); dp = ImageDraw.Draw(pn)
dp.rounded_rectangle((0, 0, 708 * SS - 1, 620 * SS - 1), radius=8 * SS, fill=(11, 12, 14, 255), outline=(58, 55, 50, 255), width=2 * SS)
dp.rounded_rectangle((4 * SS, 4 * SS, 704 * SS - 1, 616 * SS - 1), radius=6 * SS, outline=(26, 26, 29, 255), width=SS)
pn.resize((708, 620), Image.LANCZOS).save(os.path.join(OUT, "kb_panel.png"))
print("keyboard art written")
