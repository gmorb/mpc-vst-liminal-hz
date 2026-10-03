#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_test_wavs.py <out dir> -- WAV variants that IR libraries ship, for tools/wav_test.cpp: PCM 16/24/32, float
32/64, EXTENSIBLE with and without a `fact` chunk, extra chunks before fmt and after data, stereo, 96 kHz."""
import math, os, random, struct, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
GUID = {1: b'\x01\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71',
        3: b'\x03\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71'}
def wav(name, nch=1, rate=48000, bits=24, fmt=1, n=4000, ext=False, fact=False, before=(), after=()):
    random.seed(len(name))
    fr = bytearray()
    for i in range(n):
        for c in range(nch):
            v = random.gauss(0, 0.2) * math.exp(-3 * i / n) * (1.0 if c == 0 else -0.5)
            v = max(-0.999, min(0.999, v))
            if fmt == 3: fr += struct.pack('<f' if bits == 32 else '<d', v)
            elif bits == 16: fr += struct.pack('<h', int(v * 32767))
            elif bits == 24: fr += struct.pack('<i', int(v * 8388607))[:3]
            else: fr += struct.pack('<i', int(v * 2147483647))
    ba = nch * bits // 8
    if ext:
        fc = struct.pack('<HHIIHH', 0xFFFE, nch, rate, rate * ba, ba, bits) + struct.pack('<HHI', 22, bits, 4 if nch == 1 else 3) + GUID[fmt]
    else:
        fc = struct.pack('<HHIIHH', fmt, nch, rate, rate * ba, ba, bits)
    def chunk(cid, data): return cid + struct.pack('<I', len(data)) + data + (b'\x00' if len(data) % 2 else b'')
    body = b'WAVE' + b''.join(chunk(c, d) for c, d in before) + chunk(b'fmt ', fc)
    if fact: body += chunk(b'fact', struct.pack('<I', n))
    body += chunk(b'data', bytes(fr)) + b''.join(chunk(c, d) for c, d in after)
    open(os.path.join(out, name + '.wav'), 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
wav('pcm16', bits=16); wav('pcm24'); wav('pcm32', bits=32); wav('float32', bits=32, fmt=3); wav('float64', bits=64, fmt=3)
wav('ext_pcm24', ext=True); wav('ext_pcm24_fact', ext=True, fact=True); wav('ext_float32', bits=32, fmt=3, ext=True)
wav('ext_float32_fact', bits=32, fmt=3, ext=True, fact=True)
wav('chunks', before=[(b'JUNK', b'\x00' * 28), (b'bext', b'x' * 601)], after=[(b'LIST', b'INFOISFT\x05\x00\x00\x00Logic\x00')])
wav('rate96k', rate=96000); wav('stereo24', nch=2); wav('stereo_ext_float', nch=2, bits=32, fmt=3, ext=True)
print('test wavs in', out)
