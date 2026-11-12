#!/usr/bin/env python3
"""Fails when an embedded ESSL shader blob (shaderc --bin2c header) contains mediump or lowp anywhere.

GPUs differ in what mediump means: Adreno and Mali run it as fp16, others as fp32. Positions, depth, time and the
HDR / tone / occlusion maths overflow or band in fp16, so every shader of the engine is highp. shaderc's
spirv-cross output declares most variables with an explicit qualifier, but not all (OpUndef placeholders, for
example), so its default precision is highp too (patches/bgfx-essl-highp-default.patch) and the default line
`precision mediump float;` is an error like any other mediump. A shader that needs mediump for speed must be
added here with a reason.
  essl_precision_check.py <stamp> <blob.bin.h>...
"""
import pathlib
import re
import sys

stamp, *blobs = sys.argv[1:]
problems = []
for blob in blobs:
    text = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', pathlib.Path(blob).read_text())).decode('latin1')
    start = text.find('precision ')
    if start < 0:
        continue  # not an ESSL blob
    for line in text[start:].split('\n'):
        if re.search(r'\b(mediump|lowp)\b', line):
            problems.append(f'{pathlib.Path(blob).name}: {line.strip()[:120]}')
if problems:
    sys.exit('Shader precision check failed (only highp is allowed, see this script):\n  ' + '\n  '.join(problems))
pathlib.Path(stamp).write_text(f'{len(blobs)} shader blobs checked\n')
