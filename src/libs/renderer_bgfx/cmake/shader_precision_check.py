#!/usr/bin/env python3
"""Fails when an embedded shader blob (shaderc --bin2c header) is not highp everywhere.

GPUs differ in what mediump means: Adreno and Mali run it as fp16, others as fp32. Positions, depth, time and the
HDR / tone / occlusion maths overflow or band in fp16, so every shader of the engine is highp. A shader that needs
mediump for speed must be added here with a reason.

ESSL blobs (OpenGL ES, <name>.bin.h): no `mediump` or `lowp` anywhere. shaderc's spirv-cross output declares most
variables with an explicit qualifier, but not all (OpUndef placeholders, for example), so its default precision is
highp too (patches/bgfx-essl-highp-default.patch) and the default line `precision mediump float;` is an error like
any other mediump.

SPIR-V blobs (Vulkan, <name>.spv.bin.h): no RelaxedPrecision decoration, which is what mediump / lowp and HLSL's
min16float become there and what lets a driver compute in fp16.
  shader_precision_check.py <stamp> <blob.bin.h>...
"""
import pathlib
import re
import struct
import sys

SPIRV_MAGIC = struct.pack('<I', 0x07230203)
OP_DECORATE, OP_MEMBER_DECORATE, RELAXED_PRECISION = 71, 72, 0


def relaxed_precision(data, name):
    """The RelaxedPrecision decorations of the SPIR-V module inside a bgfx shader blob."""
    start = data.find(SPIRV_MAGIC)
    if start < 0:
        return [f'{name}: no SPIR-V module in the blob']
    # bgfx writes the module's byte size right before it.
    size = struct.unpack_from('<I', data, start - 4)[0]
    words = struct.unpack_from(f'<{size // 4}I', data, start)
    found = 0
    at = 5  # header: magic, version, generator, bound, schema
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 0xFFFF
        if count == 0:
            return [f'{name}: malformed SPIR-V at word {at}']
        if (op == OP_DECORATE and words[at + 2] == RELAXED_PRECISION) or \
                (op == OP_MEMBER_DECORATE and words[at + 3] == RELAXED_PRECISION):
            found += 1
        at += count
    return [f'{name}: {found} RelaxedPrecision decorations'] if found else []


stamp, *blobs = sys.argv[1:]
problems = []
for blob in blobs:
    path = pathlib.Path(blob)
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', path.read_text()))
    if path.name.endswith('.spv.bin.h'):
        problems += relaxed_precision(data, path.name)
        continue
    text = data.decode('latin1')
    start = text.find('precision ')
    if start < 0:
        continue  # no precision statement: a vertex shader, highp by default
    for line in text[start:].split('\n'):
        if re.search(r'\b(mediump|lowp)\b', line):
            problems.append(f'{path.name}: {line.strip()[:120]}')
if problems:
    sys.exit('Shader precision check failed (only highp is allowed, see this script):\n  ' + '\n  '.join(problems))
pathlib.Path(stamp).write_text(f'{len(blobs)} shader blobs checked\n')
