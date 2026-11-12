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
    ("shReadout_600x42", "Value", 32.0, IVORY),     # the sonic summary
    # the IR List and My Presets, laid over the page
    ("shRow_", "Value", 30.0, IVORY),               # a list row (an IR, a saved preset)
    ("shReadout_226x36", "Value", 28.0, GREY),      # the list's title ("IRs 7-12 of 83")
    ("shReadout_520x36", "Value", 27.0, GREY),      # My Presets' status line
    ("shReadout_236x34", "Value", 26.0, GREY),      # LONG IR's status line ("Off · IRs up to 5 s")
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
json.dump(t, open(p, "w"), indent=1)
print("skin_post: %d type roles set; %d buttons switch once per tap (rows as the kit makes them)" % (len(done), fixed))
