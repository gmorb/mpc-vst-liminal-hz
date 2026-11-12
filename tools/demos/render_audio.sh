#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# The README's demo videos (docs/demos/), made from the plugin itself:
#   vst/build.sh host                 (the PC plugin and the page; needs mpc-vst-plugins as for the build)
#   tools/demos/render_audio.sh       (this: dry phrases, then the plugin's real output and page state)
#   for d in pad drums hits keys stabs pipe grain chord breath ball duck sync; do python3 tools/demos/make_video.py $d docs/demos/liminal-hz-demo-$d.mp4; done
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
go() { local name=$1 src=$2; shift 2                # go <render name> <dry phrase> <preset> [key=value ...]
  for try in 1 2 3 4 5 6; do "$B/demo_host" "$SO" "$B/$src.wav" "$B/${name}_wet.wav" "$B/$name.json" "$@" && { echo "$name: clean (try $try)"; return 0; }; done
  echo "$name: FAILED (late tail blocks in every try: a busy PC?)" >&2; return 1; }
go pad pad 4 decay=300 mix=55 feedback=40 predelay=30 width=160 output=-9 long_ir=1   # Backrooms (preset 4)
go drums drums 1 output=2                                                         # Empty Mall (preset 1)
go hits hits 11 mix=55 output=5                                                   # Reverse Bloom (preset 11)
go keys keys 7                                                                    # Dream Ping-Pong (preset 7)
go stabs stabs 14 mix=60 output=-6                                                # Liminal Hz (preset 14)
# 1.4: the Textures presets (15-19), Duck and SYNC
go pipe perc 15 output=0.5                                                           # Struck Pipe
go grain vox 16 output=-3.5                                                         # Grain Cloud
go chord perc 17 output=0.5                                                       # Tuned Chord
go breath hats 18 output=3.5                                                      # Breath
go ball snaps 19 output=1.5                                                         # Bouncing Ball
go duck0 drums 4 mix=45 decay=200 duck=0 output=-4                               # Backrooms, Duck off ...
go duck80 drums 4 mix=45 decay=200 duck=80 output=4                              # ... and at 80% (each matched to the dry)
go sync_off perc 8 predelay=70 output=2                                           # Parking Slap, 70 ms ...
go sync_on perc 8 predelay_sync=1 predelay=225 bpm=100 output=2                   # ... and 1/8 D at 100 BPM
# 1.4 sound design: experimental sources pushed somewhere strange (levels matched to the dry, peaks under -1.5 dBFS)
go ghost whisper 11 stretch=150 mix=70 feedback=35 width=160 output=1             # Reverse Bloom: whispers to ghosts
go drips drips 15 stretch=200 mix=70 feedback=35 decay=200 output=6.5             # Struck Pipe: a metal cave
go wind wind 17 mix=85 decay=300 feedback=40 output=-2                            # Tuned Chord: wind that sings
go ticks ticks 19 mix=75 feedback=45 predelay_sync=1 predelay=225 bpm=90 output=5.5   # Bouncing Ball: a glitch cascade
go grains riser 16 stretch=200 mix=85 feedback=45 decay=250 output=-6             # Grain Cloud: a riser torn apart
go hum hum 18 mix=70 feedback=55 decay=200 output=1.5                             # Breath: a flickering tube, breathing
