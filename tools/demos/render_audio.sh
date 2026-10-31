#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# The README's demo videos (docs/demos/), made from the plugin itself:
#   vst/build.sh host                 (the PC plugin and the page; needs mpc-vst-plugins as for the build)
#   tools/demos/render_audio.sh       (this: dry phrases, then the plugin's real output and page state)
#   for d in pad drums hits; do python3 tools/demos/make_video.py $d docs/demos/liminal-hz-demo-$d.mp4; done
#   python3 tools/demos/make_posters.py   (the README thumbnails)
# gen_audio.py synthesizes the dry phrases (no samples). demo_host runs vst/build/host/liminal_hz.so over them in real
# time (128-frame blocks, as MPC) and records the page; a render with a late tail block (a busy PC) is run again.
# Each demo's settings are below. The Output values match the wet level to the dry one (so it isn't louder-is-better).
set -u
cd "$(dirname "$0")/../.."
B=vst/build/demos; mkdir -p "$B"
SO=vst/build/host/liminal_hz.so
[ -f "$SO" ] || { echo "run vst/build.sh host first" >&2; exit 1; }
g++ -O2 -std=c++17 -w tools/demos/demo_host.cpp -o "$B/demo_host" -ldl -lpthread || exit 1
python3 tools/demos/gen_audio.py "$B" || exit 1
export MPCNAM_FACTORY="$PWD/packaging/factory" MPCNAM_MEDIA="$(mktemp -d)"   # (no cards: only the factory IRs)
go() { local src=$1; shift
  for try in 1 2 3 4 5 6; do "$B/demo_host" "$SO" "$B/$src.wav" "$B/${src}_wet.wav" "$B/$src.json" "$@" && { echo "$src: clean (try $try)"; return 0; }; done
  echo "$src: FAILED (late tail blocks in every try: a busy PC?)" >&2; return 1; }
go pad 4 decay=300 mix=55 feedback=40 predelay=30 width=160 output=-9 long_ir=1   # Backrooms (preset 4)
go drums 1 output=2                                                               # Empty Mall (preset 1)
go hits 11 mix=55 output=5                                                        # Reverse Bloom (preset 11)
