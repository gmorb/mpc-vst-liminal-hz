#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# All tests on this PC (vst/build.sh host first, or let this do it):
#   convolution   engine/long_convolver against direct convolution (uniform, inline and threaded tails)
#   engine_test   the reverb chain: pass-through, mix, pre-delay, mono/stereo IRs, width, levels, crossfade, blocks
#   pkce_test, http_abi_check   TONE3000 sign-in pieces (PKCE vs hashlib; libcurl constants vs curl.h)
#   library_test  the reverbs/ search on a made-up /media
#   plugin_test   the .so as MPC uses it, with tools/t3k_mock.py standing in for TONE3000
# SANITIZE=asan|tsan: the plugin and plugin_test with AddressSanitizer+UBSan or ThreadSanitizer.
# ARM=1: also plugin_test against the device .so (vst/build/liminal_hz.so) under qemu-arm.
set -euo pipefail
cd "$(dirname "$0")/.."
T="$PWD/vst/.test"; rm -rf "$T"; mkdir -p "$T"
python3 tools/make_test_wavs.py "$T/wavs" > /dev/null
FL="-O2 -std=c++17 -w -D_FILE_OFFSET_BITS=64 -Iengine -Iengine/pffft -Isrc/audiodsptools -Isrc/nlohmann"
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
fi

if [ -n "$SAN" ]; then HOST_FLAGS="-O1 $SAN" JOBS="${JOBS:-3}" vst/build.sh host > "$T/build.log" 2>&1 || { tail -20 "$T/build.log"; exit 1; }
# (the PC plugin is rebuilt when it's missing or older than any source: never test a stale build)
elif [ ! -f vst/build/host/liminal_hz.so ] || [ -n "$(find engine vst src -newer vst/build/host/liminal_hz.so \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' -o -name '*.json' -o -name '*.conf' -o -name '*.py' \) -not -path 'vst/build/*' 2>/dev/null | head -1)" ]; then vst/build.sh host > "$T/build.log" 2>&1 || { tail -20 "$T/build.log"; exit 1; }; fi
g++ -O1 -std=c++17 -w $SAN tools/plugin_test.cpp -o "$T/plugin_test" -ldl -lpthread
mkdir -p "$T/mock"; cp "$T/wavs/stereo_ext_float.wav" "$T/mock/ir.wav"; cp "$T/wavs/pcm24.wav" "$T/mock/a2.nam"
python3 tools/t3k_mock.py 18997 "$T/mock" "$KEY" & MOCK=$!; sleep 1
run "plugin_test${SANITIZE:+_$SANITIZE}" env ASAN_OPTIONS=detect_leaks=1 MPCNAM_FACTORY="$PWD/packaging/factory" MPCNAM_T3K_API=http://127.0.0.1:18997/api/v1 \
    MPCNAM_T3K_PORT=18098 MPCNAM_T3K_HOST=127.0.0.1 "$T/plugin_test" vst/build/host/liminal_hz.so "$T/wavs"
kill $MOCK 2>/dev/null || true

if [ "${ARM:-0}" = 1 ]; then
  ZIG="${ZIG:-python3 -m ziglang}"
  $ZIG c++ -target arm-linux-gnueabihf.2.36 -mcpu=cortex_a17 -O2 -std=c++17 -w tools/plugin_test.cpp -o "$T/plugin_test_arm" -ldl -lpthread 2>/dev/null
  run plugin_test_arm env MPCNAM_FACTORY="$PWD/packaging/factory" QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf qemu-arm "$T/plugin_test_arm" "$PWD/vst/build/liminal_hz.so" "$T/wavs"
fi
exit $fail
