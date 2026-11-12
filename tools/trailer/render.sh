#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# The trailer (made from the plugin itself, like the demos):
#   vst/build.sh host                              (the PC plugin and the page)
#   tools/trailer/render.sh dark|cyber|trap           (this: that score's stems, each played through Liminal Hz)
#   python3 tools/trailer/mix.py dark|cyber|trap       (mixed and mastered: vst/build/trailer/<style>/mix.wav)
#   python3 tools/trailer/make_trailer.py wide|tall out.mp4 --style dark|cyber|trap   (1920x1080 ~63 s, 1080x1920 ~37 s)
# Each stem runs through tools/demos/demo_host in real time (128-frame blocks, as on MPC); a render with a late block
# (a busy PC) is run again.
set -u
cd "$(dirname "$0")/../.."
STYLE=${1:-dark}
T=vst/build/trailer/$STYLE; mkdir -p "$T"
SO=vst/build/host/liminal_hz.so
[ -f "$SO" ] || { echo "run vst/build.sh host first" >&2; exit 1; }
g++ -O2 -std=c++17 -w tools/demos/demo_host.cpp -o "$T/demo_host" -ldl -lpthread || exit 1
python3 tools/trailer/score.py "$STYLE" "$T" > /dev/null || exit 1
export MPCNAM_FACTORY="$PWD/packaging/factory" MPCNAM_MEDIA="$(mktemp -d)"
python3 -c "import sys; sys.path.insert(0, 'tools/trailer'); import score
for k, v in score.STEMS['$STYLE'].items(): print(k, v or '-')" | while read -r stem args; do
  [ "$args" = "-" ] && { echo "$stem: dry"; continue; }
  for try in 1 2 3 4 5 6; do
    # shellcheck disable=SC2086
    "$T/demo_host" "$SO" "$T/$stem.wav" "$T/${stem}_wet.wav" "$T/$stem.json" $args > /dev/null && { echo "$stem: clean (try $try)"; break; }
  done
done
