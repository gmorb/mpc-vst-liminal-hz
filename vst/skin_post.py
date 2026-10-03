#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""skin_post.py <Plugin Skins dir> -- after mpc-vst-plugins' gen_vst.py: the live text's type roles. MPC draws these
labels itself (Titillium Web); only their size and colour are set here, per component definition and label name.
Roles (from the design pass): what you read is bright, what names it is quiet, and amber is kept for live values
(knob arcs, the playhead), never for plain text.
  IR name 30 ivory, preset 22 ivory, knob values 19 ivory, pack 20 grey, details 18 grey, TONE3000 status 20 grey,
  knob and switch names 13 dim."""
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
    ("shReadout_360x36", "Value", 27.0, GREY),      # TONE3000 status
    ("shReadout_600x42", "Value", 32.0, IVORY),     # the sonic summary
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
json.dump(t, open(p, "w"), indent=1)
print("skin_post: %d type roles set" % len(done))
