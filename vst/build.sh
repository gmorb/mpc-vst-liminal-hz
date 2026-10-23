#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# Build Liminal Hz.
#   vst/build.sh            the Gen 1 device plugin: armhf (Zig, glibc 2.36, Cortex-A17, NEON FFT) + its page (MPC skin)
#   vst/build.sh gen2       the Gen 2 device plugin: aarch64 (Zig, glibc 2.36, RK3588's A76/A55: ARMv8.2-A, NEON FFT)
#                           with the high IR quality always on (engine/platform.h: 20 s IRs, sinc resampling)
#   vst/build.sh host       the same plugin for this PC (tests); HOST_FLAGS adds flags (e.g. sanitizers)
#   vst/build.sh host-gen2  the PC plugin with the Gen 2 limits (tests: tools/test.sh with GEN2=1)
# Needs: mpc-vst-plugins in $MPC_VST (default ../mpc-vst-plugins), python3 + Pillow, Eigen 3.4 headers
# a host g++, Playwright + Chromium (the page renderer), and Zig (pip install ziglang) for the device build.
# Output: vst/build/liminal_hz.so (Gen 1), vst/build/aarch64/liminal_hz.so (Gen 2), vst/build/host[-gen2]/liminal_hz.so,
#         vst/build/skin/, pluginlist-entry.xml (the page is the same for both generations)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
MPC_VST="${MPC_VST:-$ROOT/../mpc-vst-plugins}"
MODE="${1:-device}"
B="$ROOT/vst/build"
[ -f "$MPC_VST/tools/gen_vst.py" ] || { echo "MPC_VST=$MPC_VST is not an mpc-vst-plugins checkout" >&2; exit 1; }

# the page and params.h (mpc-vst-plugins), frame titles in Titillium Web (OFL, from the kit)
mkdir -p "$B"
gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -o "$B/shadow_art" "$MPC_VST/tools/shadow_art.c" -lm
rm -rf "$B/skin"
# the page's artwork: the browser renderer (vst.json "art": "html": headless Chromium via Playwright), which
# draws real Titillium text, the TONE3000 logo and the T3K mark; SHADOW_ART would override it, so it isn't set
python3 "$MPC_VST/tools/gen_vst.py" vst/vst.json
python3 vst/skin_post.py "$B/skin/Gm0rb - VST - Liminal Hz/Plugin Skins"     # the live-text sizes and colours
sed -i -e 's/category="Synth"/category="Effect"/' -e 's/isInstrument="1"/isInstrument="0"/' \
       -e 's/numInputs="0"/numInputs="2"/' "$B/pluginlist-entry.xml"

SRCS_C="engine/pffft/pffft.c engine/pffft/pffft_common.c"
SRCS_CXX="engine/long_convolver.cpp engine/reverb_engine.cpp engine/wav_reader.cpp engine/library.cpp
          engine/presets.cpp engine/presets_web.cpp engine/tone3000.cpp engine/http.cpp engine/pkce.cpp vst/liminal_hz_vst.cpp"
INC="-Iengine -Iengine/pffft -Isrc/audiodsptools -Isrc/nlohmann -I$B"
DEFS=""
case "$MODE" in gen2|host-gen2) DEFS="-DLIMINAL_GEN2" ;; esac
# -fsigned-char: WDL (the resampler's helpers) requires a signed char, which ARM Linux doesn't default to
COMMON="-O3 -fPIC -fvisibility=hidden -fsigned-char -D_FILE_OFFSET_BITS=64 -w"

JOBS="${JOBS:-4}"   # parallel compiles; -O3 C++ needs ~1 GB each, so don't start them all at once
PIDS=()
throttle() { while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n || true; done; }
run() { "$@" || { echo "error: compile failed: ${*: -1}" >&2; return 1; }; }

build() {   # $1 out dir, $2 C compiler, $3 C++ compiler, $4 extra flags, $5 link flags
  local out="$1" cc="$2" cxx="$3" fl="$4" lf="$5" objs=() pids=() failed=0
  rm -rf "$out/obj"            # never link a stale object: every build starts clean
  mkdir -p "$out/obj"
  for f in $SRCS_C; do
    o="$out/obj/c_$(basename "${f%.c}").o"; throttle
    run $cc $COMMON $fl $DEFS -Iengine/pffft -c "$f" -o "$o" & pids+=($!); objs+=("$o")
  done
  for f in $SRCS_CXX; do
    p=our
    o="$out/obj/${p}_$(basename "${f%.cpp}").o"; throttle
    run $cxx $COMMON $fl -std=c++17 $DEFS $INC -c "$f" -o "$o" & pids+=($!); objs+=("$o")
  done
  for pid in "${pids[@]}"; do wait "$pid" || failed=1; done    # every compile's own exit status
  [ "$failed" = 0 ] || { echo "error: a compile failed (see above)" >&2; exit 1; }
  $cxx $fl -shared -o "$out/liminal_hz.so" "${objs[@]}" -Wl,--version-script=vst/exports.map $lf
}

if [ "$MODE" = host ] || [ "$MODE" = host-gen2 ]; then
  build "$B/$MODE" gcc g++ "${HOST_FLAGS:-}" "-lpthread -ldl"
  echo "host plugin: $B/$MODE/liminal_hz.so"
else
  ZIG="${ZIG:-python3 -m ziglang}"
  if [ "$MODE" = gen2 ]; then
    # cortex_a55: ARMv8.2-A features both of the RK3588's core types have (the scheduler moves threads between them);
    # PFFFT_ENABLE_NEON: PFFFT's NEON path (without it PFFFT builds scalar, as the Gen 1 build always has)
    TGT="-target aarch64-linux-gnu.2.36 -mcpu=cortex_a55 -DPFFFT_ENABLE_NEON"
    build "$B/device-aarch64" "$ZIG cc $TGT" "$ZIG c++ $TGT" "" "-Wl,-s -Wl,--no-undefined -lpthread"
    mkdir -p "$B/aarch64"; cp "$B/device-aarch64/liminal_hz.so" "$B/aarch64/liminal_hz.so"
    SO="$B/aarch64/liminal_hz.so"; WANT_ARCH="ARM aarch64"; CEIL=GLIBC_2.39
  else
    # PFFFT_ENABLE_NEON: PFFFT's NEON path (before 1.3.0 this build ran PFFFT scalar: the flag was never set)
    TGT="-target arm-linux-gnueabihf.2.36 -mcpu=cortex_a17 -DPFFFT_ENABLE_NEON"
    build "$B/device" "$ZIG cc $TGT" "$ZIG c++ $TGT" "" "-Wl,-s -Wl,--no-undefined -lpthread"
    cp "$B/device/liminal_hz.so" "$B/liminal_hz.so"
    SO="$B/liminal_hz.so"; WANT_ARCH="ARM, EABI5"; CEIL=GLIBC_2.36
  fi
  EXP=$(readelf --dyn-syms -W "$SO" | awk '$5=="GLOBAL" && $7!="UND" && $4!="NOTYPE"{print $8}' | tr '\n' ' ')
  GLIBC=$(readelf -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
  NEEDED=$(readelf -d "$SO" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
  echo "device plugin: $SO"
  echo "   $(file -b "$SO" | cut -d, -f1-3)"
  echo "   exported: $EXP"
  echo "   needed:   $NEEDED"
  echo "   highest glibc: $GLIBC"
  [ "$EXP" = "VSTPluginMain " ] || { echo "error: unexpected exports" >&2; exit 1; }
  case "$NEEDED" in *libstdc++*|*libc++*) echo "error: C++ runtime must be linked in" >&2; exit 1 ;; esac
  file -b "$SO" | grep -q "$WANT_ARCH" || { echo "error: not a $WANT_ARCH library" >&2; exit 1; }
  [ "$(printf '%s\n%s\n' "$GLIBC" "$CEIL" | sort -V | tail -1)" = "$CEIL" ] || { echo "error: needs glibc > ${CEIL#GLIBC_}" >&2; exit 1; }
fi
