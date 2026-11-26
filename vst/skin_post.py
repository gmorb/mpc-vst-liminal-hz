#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""skin_post.py <Plugin Skins dir> -- after mpc-vst-plugins' gen_vst.py: the live text's type roles. MPC draws these
labels itself (Titillium Web); only their size and colour are set here, per component definition and label name.
Roles (from the design pass): what you read is bright, what names it is quiet, and amber is kept for live values
(knob arcs, the playhead), never for plain text.
  IR name 30 ivory, preset 22 ivory, knob values 19 ivory, pack 20 grey, details 18 grey, TONE3000 status 20 grey,
  knob and switch names 13 dim. (The numbers in ROLES are what is set: larger, as MPC draws text smaller than it looks.)"""
import json, sys
IVORY, GREY, DIM = "ffe6dfd2", "ff9a958c", "ff7a746a"
ROLES = [  # (definition key prefix, label name, height, colour) -- large: MPC draws these smaller than they look in mockups
    ("shStepText_", "Value", 52.0, IVORY),
    ("shPopField_", "Value", 40.0, IVORY),
    ("shKnob", "Value", 38.0, IVORY),
    ("shKnob", "Name", 27.0, GREY),
    ("shToggle", "Name", 25.0, GREY),
    ("shReadout_500x42", "Value", 34.0, GREY),      # the pack
    ("shReadout_420x38", "Value", 30.0, GREY),      # the detail line
    ("shReadout_288x36", "Value", 27.0, GREY),      # TONE3000 status (beside BROWSE)
    ("shReadout_400x42", "Value", 27.0, IVORY),     # the sonic summary (400 wide beside the tabs)
    # the IR List and My Presets, laid over the page
    ("shRow_", "Value", 30.0, IVORY),               # a list row (an IR, a saved preset)
    ("shReadout_226x36", "Value", 28.0, GREY),      # the list's title ("IRs 7-12 of 83")
    ("shReadout_520x36", "Value", 27.0, GREY),      # My Presets' status line
    ("shReadout_236x34", "Value", 26.0, GREY),      # LONG IR's status line ("Off · IRs up to 5 s")
    # the keyboard (laid over the right-hand side): the name being typed is the brightest thing on it
    ("shReadout_664x36", "Value", 26.0, GREY),      # its status line ("Up to 32 characters", "That name is taken...")
    ("shReadout_664x36_", "Value", 28.0, GREY),     # its title ("Name the new preset", "Rename Big Hall")
    ("shReadout_664x60_", "Value", 48.0, IVORY),    # the name, as typed
    ("shRow_61x74_", "Value", 40.0, IVORY),         # a letter, a digit, SHIFT ("abc" / "Abc" / "ABC"), DEL
    ("shRow_118x74_", "Value", 27.0, IVORY),        # CLEAR, CANCEL
    ("shRow_268x74_", "Value", 27.0, GREY),         # SPACE
    ("shRow_142x74_", "Value", 29.0, IVORY),        # SAVE
    ("shRow_88x30_", "Value", 30.0, IVORY),         # the SHAPE / ROOM / SCAN tabs
]



p = sys.argv[1].rstrip("/") + "/TUI.json"
t = json.load(open(p))
done = set()
for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
    for prefix, name, h, col in ROLES:
        if not d["key"].startswith(prefix):
            continue
        for c in d["value"].get("componentsData", []):
            cd = c["componentData"]
            if cd.get("type") == "Label" and cd.get("name") == name:
                cd["data"]["textStyle"]["font"]["height"] = h
                cd["data"]["textStyle"]["colour"] = col
                done.add((prefix, name))
missing = {(p_, n) for p_, n, _, _ in ROLES} - done
if missing:
    sys.exit("skin_post: no component for %s (layout changed?)" % sorted(missing))

# One tap, one press (issue #5). The kit's buttons (shTrig_*) switch their value twice per tap: a "Toggle Switch"
# action on Mouse Down, and the Button component inside, which switches it again. The plugin reads them back as 0, so
# both writes were presses: NEXT PACK, PREV/NEXT in the lists, DELETE... each took two steps. REVERSE and LONG IR
# (shToggle) have the same Button and send Mouse Down to "Q-Link" instead: one tap, one switch. The buttons get the
# same. (Enter on the encoder keeps "Toggle Switch".)
# NOT the list rows (shRow_*): with Mouse Down on "Q-Link" a tap on a row did nothing on the device (an IR or a My
# Preset couldn't be picked). They keep the kit's action; a row picked twice is the same pick (the same IR: no
# change; the same My Preset: loaded again as saved).
fixed = 0
for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
    v = d["value"]
    has_button = any(c["componentData"].get("type") == "Button" for c in v.get("componentsData", []))
    for a in v.get("actions", []):
        if has_button and a["onAction"] == "Mouse Down" and a["handler"] == "Toggle Switch":
            if d["key"].startswith("shRow_"):
                continue
            if not d["key"].startswith("shTrig_"):
                sys.exit("skin_post: %s switches twice per tap, and isn't a button or a row" % d["key"])
            a["handler"] = "Q-Link"
            fixed += 1
if fixed == 0:
    sys.exit("skin_post: no button switched on Mouse Down (the kit changed? check issue #5's fix)")
# SCAN's drag strip: the slider laid over the painting (668 x 148) is touch only. Its film strip becomes clear (the
# painting shows through; the glow under it shows the position) and its value text goes (POSITION's knob shows it).
import os
from PIL import Image
strip = 0
for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
    if not d["key"].startswith("shSlider_h_668x148"):
        continue
    v = d["value"]
    for c in v.get("componentsData", []):
        cd = c["componentData"]
        fs = cd.get("data", {}).get("filmStrip")
        if cd.get("type") == "Knob" and fs:
            # (test 5's photos: on the device the painting under it vanished. The kit's strip was 83 frames, 668 x
            #  12284 px, ~32 MB as a texture, far bigger than any other picture: now 2 clear frames under a new name)
            f = os.path.join(os.path.dirname(p), fs)
            os.remove(f)
            clear = "sh_scan_drag_clear.png"
            Image.new("RGBA", (668, 148 * 2), (0, 0, 0, 0)).save(os.path.join(os.path.dirname(p), clear))
            cd["data"]["filmStrip"] = clear
            cd["data"]["numFrames"] = 2
            strip += 1
    v["componentsData"] = [c for c in v.get("componentsData", []) if c["componentData"].get("type") != "Label"]
if strip != 1:
    sys.exit("skin_post: SCAN's drag strip not found (layout changed?)")
# The painting must stay on top: a page's baked art (an opaque picture cut from the background) drawn after the
# painting's columns or the strip under it covers them (tests 5-5c: the SCAN tab's tiles stretched its art over
# columns 2-12). Only the keyboard's panel may cover them.
import re
_keys = [x["key"] for x in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "params.json")))["params"]]
_root = [d for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"] if d["key"].endswith("|LIMINAL")]
_modes = [c for c in (_root[0]["value"]["componentsData"] if _root else []) if c["componentData"]["name"] == "Mode"]
def _box(c):
    x, y, w, h = (float(v) for v in c["bounds"]["bounds"].split())
    return x, y, x + w, y + h
_paint = []
for k, c in enumerate(_modes):
    h = c["bounds"]["additionalInvalidatingHandles"]
    m = re.match(r"IndexedEnabling/\d+/\d+/Parameter (\d+)", h[0]) if h else None
    key = _keys[int(m.group(1))] if m else "?"
    if key.startswith(("wave_", "spark_")):
        _paint.append(_box(c))
    elif _paint and key != "kb":
        a0, b0, a1, b1 = _box(c)
        for x0, y0, x1, y1 in _paint:
            if a0 < x1 and x0 < a1 and b0 < y1 and y0 < b1:
                sys.exit("skin_post: %s's art (picture %d, %s) is drawn over the painting: move its first widget above "
                         "the painting's pictures in layout.conf" % (key, k, c["bounds"]["bounds"]))
if not _paint:
    sys.exit("skin_post: the painting's pictures not found (layout changed?)")
json.dump(t, open(p, "w"), indent=1)
print("skin_post: %d type roles set; %d buttons switch once per tap (rows as the kit makes them)" % (len(done), fixed))
