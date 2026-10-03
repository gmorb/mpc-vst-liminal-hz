#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""param_json.py <out.json> -- the parameter description in the Force VST repository's <Name>.json format, from
vst/params.json (the VST parameter order)."""
import json, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
params = json.load(open(os.path.join(ROOT, "vst", "params.json")))["params"]
NOTES = {"ir": "the IR in use (text); Q-Link or the arrows browse the reverbs/ folders",
         "ir_info": "the IR's place in its pack, or a status (read-only)", "ir_pack": "its pack (read-only)",
         "length": "the share of the IR used (up to 5 s): shorter = less CPU on the second core",
         "ir_src": "Local or TONE3000 (shows the T3K mark; read-only)", "rescan": "look for new files"}
doc = {"name": "Liminal Hz", "numParams": len(params), "parameters": {}}
for i, p in enumerate(params):
    opts = p.get("options")
    kind = "steps" if opts else "button" if p.get("momentary") else "text" if p.get("display") == "string" else "knob"
    doc["parameters"][str(i)] = {"index": i, "caseLabel": p["key"], "name": p["name"], "label": p.get("unit"),
        "variable": p["key"], "transform": "value", "range": [0, len(opts) - 1] if opts else [p["min"], p["max"]],
        "type": kind, "displayValues": opts, "unit": p.get("unit") or ("enum" if opts else None),
        "display": {"format": "list" if opts else "text" if kind == "text" else "%.0f", "note": NOTES.get(p["key"], "")},
        "default": p.get("default"), "confidence": 1.0, "override_safe": True}
json.dump(doc, open(sys.argv[1], "w"), indent=1)
