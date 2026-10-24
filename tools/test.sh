#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# All tests on this PC (vst/build.sh host first, or let this do it):
#   convolution   engine/long_convolver against direct convolution (uniform, inline and threaded tails)
#   engine_test   the reverb chain: pass-through, mix, pre-delay, mono/stereo IRs, width, levels, crossfade, blocks
#   pkce_test, http_abi_check   TONE3000 sign-in pieces (PKCE vs hashlib; libcurl constants vs curl.h)
#   library_test  the reverbs/ search on a made-up /media
#   presets_test, presets_web_test  My Presets: the files, and the phone page over a real socket
#   plugin_test   the .so as MPC uses it, with tools/t3k_mock.py standing in for TONE3000
# SANITIZE=asan|tsan: the plugin and plugin_test with AddressSanitizer+UBSan or ThreadSanitizer.
# ARM=1: also the convolution check, engine_test and plugin_test as armv7 programs under qemu-arm (NEON FFT),
#        plugin_test against the Gen 1 device .so (vst/build/liminal_hz.so).
# GEN2=1: everything with the Gen 2 limits (-DLIMINAL_GEN2, engine/platform.h), against vst/build/host-gen2.
# AARCH64=1: also the convolution check, engine_test and plugin_test as aarch64 programs under qemu-aarch64 (NEON FFT),
#            plugin_test against the Gen 2 device .so (vst/build/aarch64/liminal_hz.so; vst/build.sh gen2 first).
set -euo pipefail
cd "$(dirname "$0")/.."
T="$PWD/vst/.test"; rm -rf "$T"; mkdir -p "$T"
python3 tools/make_test_wavs.py "$T/wavs" > /dev/null
GDEF=""; HOSTDIR=host; [ "${GEN2:-0}" = 1 ] && { GDEF="-DLIMINAL_GEN2"; HOSTDIR=host-gen2; }
FL="-O2 -std=c++17 -w -D_FILE_OFFSET_BITS=64 $GDEF -Iengine -Iengine/pffft -Isrc/audiodsptools -Isrc/nlohmann"
PF="engine/pffft/pffft.c engine/pffft/pffft_common.c"
case "${SANITIZE:-}" in
  asan) SAN="-g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" ;;
  tsan) SAN="-g -fsanitize=thread" ;;
  *) SAN="" ;;
esac
fail=0
run() { local name="$1"; shift; echo "== $name"; if "$@" > "$T/$name.log" 2>&1; then tail -1 "$T/$name.log"; else grep -E "FAIL|ERROR|runtime error|WARNING: ThreadSanitizer" "$T/$name.log" | head -20; tail -1 "$T/$name.log"; fail=1; fi; }
KEY=$(sed -n 's/^#define MPCNAM_T3K_KEY "\(t3k_pub_[A-Za-z0-9_-]\{8,\}\)"$/\1/p' engine/tone3000.cpp)
[ -n "$KEY" ] || { echo "error: no MPCNAM_T3K_KEY in engine/tone3000.cpp" >&2; exit 1; }

if [ -z "$SAN" ]; then
  g++ $FL tools/bench_reverb.cpp engine/long_convolver.cpp $PF -o "$T/bench" -lpthread
  run convolution "$T/bench" --check
  g++ $FL tools/engine_test.cpp engine/reverb_engine.cpp engine/long_convolver.cpp engine/wav_reader.cpp $PF -o "$T/engine_test" -lpthread
  run engine_test "$T/engine_test" "$T/wavs"
  g++ $FL tools/pkce_test.cpp engine/pkce.cpp -o "$T/pkce_test"
  run pkce_test "$T/pkce_test"
  if [ -f /usr/include/curl/curl.h ] || [ -f /usr/include/x86_64-linux-gnu/curl/curl.h ]; then
    g++ $FL -c tools/http_abi_check.cpp -o "$T/http_abi_check.o"     # compiles only if the constants match
    echo "== http_abi_check"; echo "PASSED (libcurl constants match curl/curl.h)"
  fi
  g++ $FL tools/library_test.cpp engine/library.cpp -o "$T/library_test" -ldl
  run library_test "$T/library_test"
  g++ $FL tools/presets_test.cpp engine/presets.cpp engine/library.cpp -o "$T/presets_test" -ldl
  run presets_test "$T/presets_test"
  g++ $FL tools/presets_web_test.cpp engine/presets_web.cpp engine/presets.cpp engine/tone3000.cpp engine/http.cpp engine/pkce.cpp engine/library.cpp -o "$T/presets_web_test" -ldl -lpthread
  run presets_web_test "$T/presets_web_test"
fi

if [ -n "$SAN" ]; then HOST_FLAGS="-O1 $SAN" JOBS="${JOBS:-3}" vst/build.sh $HOSTDIR > "$T/build.log" 2>&1 || { tail -20 "$T/build.log"; exit 1; }
# (the PC plugin is rebuilt when it's missing or older than any source: never test a stale build)
elif [ ! -f vst/build/$HOSTDIR/liminal_hz.so ] || [ -n "$(find engine vst src -newer vst/build/$HOSTDIR/liminal_hz.so \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' -o -name '*.json' -o -name '*.conf' -o -name '*.py' \) -not -path 'vst/build/*' 2>/dev/null | head -1)" ]; then vst/build.sh $HOSTDIR > "$T/build.log" 2>&1 || { tail -20 "$T/build.log"; exit 1; }; fi
g++ -O1 -std=c++17 -w $GDEF $SAN tools/plugin_test.cpp -o "$T/plugin_test" -ldl -lpthread
mkdir -p "$T/mock"; cp "$T/wavs/stereo_ext_float.wav" "$T/mock/ir.wav"; cp "$T/wavs/pcm24.wav" "$T/mock/a2.nam"
python3 tools/t3k_mock.py 18997 "$T/mock" "$KEY" & MOCK=$!; sleep 1
run "plugin_test${SANITIZE:+_$SANITIZE}" env ASAN_OPTIONS=detect_leaks=1 MPCNAM_FACTORY="$PWD/packaging/factory" MPCNAM_T3K_API=http://127.0.0.1:18997/api/v1 \
    MPCNAM_T3K_PORT=18098 MPCNAM_T3K_HOST=127.0.0.1 "$T/plugin_test" vst/build/$HOSTDIR/liminal_hz.so "$T/wavs"
kill $MOCK 2>/dev/null || true

if [ "${ARM:-0}" = 1 ]; then
  ZIG="${ZIG:-python3 -m ziglang}"
  A32="-target arm-linux-gnueabihf.2.36 -mcpu=cortex_a17 -DPFFFT_ENABLE_NEON -O2 -w"
  QA="env QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf qemu-arm"
  zc32() { "$@" > "$T/zig32.log" 2>&1 || { grep -i error "$T/zig32.log" | head -5; echo "FAIL: armv7 compile" >&2; exit 1; }; }
  zc32 $ZIG cc $A32 -fsigned-char -Iengine/pffft -c engine/pffft/pffft.c -o "$T/pffft_a32.o"
  zc32 $ZIG cc $A32 -fsigned-char -Iengine/pffft -c engine/pffft/pffft_common.c -o "$T/pffft_common_a32.o"
  PFO32="$T/pffft_a32.o $T/pffft_common_a32.o"
  zc32 $ZIG c++ $A32 -fsigned-char -std=c++17 -Iengine -Iengine/pffft tools/bench_reverb.cpp engine/long_convolver.cpp $PFO32 -o "$T/bench_a32" -lpthread
  run convolution_arm $QA "$T/bench_a32" --check
  zc32 $ZIG c++ $A32 -fsigned-char -std=c++17 -D_FILE_OFFSET_BITS=64 -Iengine -Iengine/pffft -Isrc/audiodsptools -Isrc/nlohmann tools/engine_test.cpp engine/reverb_engine.cpp engine/long_convolver.cpp engine/wav_reader.cpp $PFO32 -o "$T/engine_test_a32" -lpthread
  run engine_test_arm $QA "$T/engine_test_a32" "$T/wavs"
  zc32 $ZIG c++ -target arm-linux-gnueabihf.2.36 -mcpu=cortex_a17 -O2 -std=c++17 -w tools/plugin_test.cpp -o "$T/plugin_test_arm" -ldl -lpthread
  run plugin_test_arm env MPCNAM_FACTORY="$PWD/packaging/factory" $QA "$T/plugin_test_arm" "$PWD/vst/build/liminal_hz.so" "$T/wavs"
fi
if [ "${AARCH64:-0}" = 1 ]; then
  ZIG="${ZIG:-python3 -m ziglang}"
  A64="-target aarch64-linux-gnu.2.36 -mcpu=cortex_a55 -DPFFFT_ENABLE_NEON -DLIMINAL_GEN2 -O2 -w"
  Q="env QEMU_LD_PREFIX=/usr/aarch64-linux-gnu qemu-aarch64"
  zc() { "$@" > "$T/zig.log" 2>&1 || { grep -i error "$T/zig.log" | head -5; echo "FAIL: aarch64 compile" >&2; exit 1; }; }
  zc $ZIG cc $A64 -Iengine/pffft -c engine/pffft/pffft.c -o "$T/pffft_a64.o"
  zc $ZIG cc $A64 -Iengine/pffft -c engine/pffft/pffft_common.c -o "$T/pffft_common_a64.o"
  PFO="$T/pffft_a64.o $T/pffft_common_a64.o"
  zc $ZIG c++ $A64 -std=c++17 -Iengine -Iengine/pffft tools/bench_reverb.cpp engine/long_convolver.cpp $PFO -o "$T/bench_a64" -lpthread
  run convolution_aarch64 $Q "$T/bench_a64" --check
  zc $ZIG c++ $A64 -std=c++17 -D_FILE_OFFSET_BITS=64 -Iengine -Iengine/pffft -Isrc/audiodsptools -Isrc/nlohmann tools/engine_test.cpp engine/reverb_engine.cpp engine/long_convolver.cpp engine/wav_reader.cpp $PFO -o "$T/engine_test_a64" -lpthread
  run engine_test_aarch64 $Q "$T/engine_test_a64" "$T/wavs"
  zc $ZIG c++ -target aarch64-linux-gnu.2.36 -mcpu=cortex_a55 -DLIMINAL_GEN2 -O2 -std=c++17 -w tools/plugin_test.cpp -o "$T/plugin_test_a64" -ldl -lpthread
  run plugin_test_aarch64 env MPCNAM_FACTORY="$PWD/packaging/factory" $Q "$T/plugin_test_a64" "$PWD/vst/build/aarch64/liminal_hz.so" "$T/wavs"
fi
exit $fail
