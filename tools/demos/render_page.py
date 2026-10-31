#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""render_page.py -- draws the Liminal Hz page as MPC does, from the built skin (TUI.json + images) and a state the
plugin reported (demo_host's JSON): knob filmstrip frames from the parameters' values, switched pictures (the IR
painting, the glow) from their options, lit buttons, and live text in Titillium Web with each label's own size,
colour, alignment and letter case. Based on mpc-vst-plugins' tools/studio.py preview."""
import json, os, re, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIN = os.path.join(ROOT, "vst/build/skin/Gm0rb - VST - Liminal Hz/Plugin Skins")   # vst/build.sh makes it
FONTS = os.environ.get("MPC_VST", os.path.join(ROOT, "..", "mpc-vst-plugins")) + "/tools/html_art/fonts/"
PX_PER_HEIGHT = 1 / 1.52          # MPC font heights are ~1.52 x CSS px (mpc-vst-plugins docs/COMMUNITY_SKINS.md)

_t = json.load(open(os.path.join(SKIN, "TUI.json")))["pageData"]
_defs = {d["key"]: d["value"] for d in _t["componentDefinitions"]["localComponentDefinitions"]}
_tab = _t["tabs"][0]
_comps = _defs[_tab["componentName"]]["componentsData"]
_img_cache = {}

def _img(name):
    if name not in _img_cache: _img_cache[name] = Image.open(os.path.join(SKIN, name)).convert("RGBA")
    return _img_cache[name]

def _xywh(b): return [int(float(v)) for v in b.split()]

def _handles(c):
    return [tuple(int(g) for g in m.groups()) for m in
            (re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)$", h) for h in c["bounds"].get("additionalInvalidatingHandles", [])) if m]

def _param(c, key="Data"):
    for m in c["handle remapping"]["map"]:
        if m["key"] == key:
            g = re.match(r"Parameter (\d+)", m["value"])
            if g: return int(g.group(1))
    return None

def _font(style, size):
    f = {"SemiBold": "TitilliumWeb-SemiBold.ttf", "Bold": "TitilliumWeb-Bold.ttf"}.get(style, "TitilliumWeb-Regular.ttf")
    return ImageFont.truetype(FONTS + f, max(6, round(size)))

def _rgb(c): return tuple(int(c[i:i + 2], 16) for i in (2, 4, 6)) if len(c) >= 8 else (200, 200, 200)

def _text(dr, box, text, ts):
    if not text: return
    if ts.get("case") == "Upper Case": text = text.upper()
    x, y, w, h = box
    f = _font(ts["font"].get("style", "Regular"), ts["font"]["height"] * PX_PER_HEIGHT)
    while dr.textlength(text, font=f) > w and len(text) > 3:   # MPC truncates what doesn't fit
        text = text[:-2].rstrip() + "…"
    j = ts.get("justification", "")
    tw = dr.textlength(text, font=f)
    tx = x if "Left" in j or j.startswith("left") else (x + w - tw if "Right" in j or j.startswith("right") else x + (w - tw) / 2)
    dr.text((tx, y + h / 2), text, font=f, fill=_rgb(ts["colour"]), anchor="lm")

def render(state):
    """state: demo_host JSON ({"params": [{i, norm, text}]}) plus "options": {param index: option index} overrides."""
    P = {p["i"]: p for p in state["params"]}
    def opt(i):
        if i in state.get("options", {}): return state["options"][i]
        for c in _comps:
            for oi, n, p in _handles(c):
                if p == i: return round(P[i]["norm"] * (n - 1))
        return 0
    im = Image.new("RGBA", (1280, 628), (0, 0, 0, 255))
    dr = ImageDraw.Draw(im)
    for c in _comps:
        if not all(opt(p) == oi for oi, n, p in _handles(c)): continue
        cd = c["componentData"]; x, y, w, h = _xywh(c["bounds"]["bounds"])
        if cd["type"] == "Image":
            im.alpha_composite(_img(cd["data"]["image"]), (x, y)); continue
        pd, pt = _param(c, "Data"), _param(c, "Text")
        for s in _defs[cd["type"]]["componentsData"]:
            sd = s["componentData"]; sx, sy, sw, sh = _xywh(s["bounds"]["bounds"])
            if sd["type"] == "Image":
                im.alpha_composite(_img(sd["data"]["image"]), (x + sx, y + sy))
            elif sd["type"] == "Knob":
                st = _img(sd["data"]["filmStrip"]); n = int(sd["data"].get("numFrames") or 127)
                n_img = n + 1 if st.size[1] == (n + 1) * st.size[0] else n
                fw, fh = st.size[0], st.size[1] // n_img
                k = min(n_img - 1, max(0, round(P[pd]["norm"] * (n_img - 1)))) if pd is not None else 0
                im.alpha_composite(st.crop((0, k * fh, fw, (k + 1) * fh)), (x + sx, y + sy))
            elif sd["type"] == "Button":
                d = sd["data"]; nb = int(d.get("numButtonsInGroup", 1)); bid = int(d.get("buttonId", 0))
                on = False
                if pd is not None and nb > 1: on = round(P[pd]["norm"] * (nb - 1)) == bid
                elif pd is not None and d.get("onImage") and not cd["type"].startswith(("shTrig", "shTap", "shBtn")):
                    on = P[pd]["norm"] > 0.5
                img = d.get("onImage") if on else d.get("offImage")
                if img: im.alpha_composite(_img(img), (x + sx, y + sy))
            elif sd["type"] == "Label":
                ts = sd["data"]["textStyle"]
                if sd["data"].get("type") == "Name":
                    _text(dr, (x + sx, y + sy, sw, sh), cd.get("name", "").strip(), ts)
                else:
                    p = pt if pt is not None else pd
                    if p is not None: _text(dr, (x + sx, y + sy, sw, sh), P[p]["text"], ts)
    return im.convert("RGB")

if __name__ == "__main__":
    st = json.load(open(sys.argv[1]))
    st["options"] = {}
    render(st).save(sys.argv[2])
