#!/usr/bin/env python3
"""Compare production packed-key decoding against pre-change BlendFrame.

Optional: --animations /path/to/RESOURCE/animation [--mode stats|diff|bench].
Only the two Bone methods are extracted to avoid loading core/filesystem/D3D.
The actual production Quaternion/SLerp/GetMatrix headers are used unchanged.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--glm-source', required=True, type=Path)
parser.add_argument('--animations', type=Path)
parser.add_argument('--mode', choices=['stats', 'diff', 'bench'], default='diff')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = (root / 'src/libs/animation/src/bone.cpp').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

with tempfile.TemporaryDirectory(prefix='storm-animation-decode-') as temporary:
    output = Path(temporary)
    decode = function('inline void Bone::GetFrame').replace('Bone::', 'Track::')
    candidate = function('void Bone::BlendFrame').replace('Bone::BlendFrame', 'Track::candidate')
    baseline = Path(__file__).with_name('baseline.inc').read_text()
    # Prevent the test driver from specializing either function for a constant
    # coefficient; production BlendFrame is also a separate compiled method.
    candidate = candidate.replace('void Track::candidate', '__attribute__((noinline)) void Track::candidate')
    baseline = baseline.replace('void Track::baseline', '__attribute__((noinline)) void Track::baseline')
    (output / 'functions.inc').write_text('#define ANI_COMPRESS_ENABLE\n' + decode + '\n' + baseline + '\n' + candidate)
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-DNDEBUG',
               '-Wno-implicit-const-int-float-conversion',
               '-I' + str(output), '-I' + str(root / 'src/libs/math/include'),
               '-I' + str(root / 'tests/glm_math/sdk_shape'), '-I' + str(args.glm_source)]
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    command += [str(Path(__file__).with_name('check.cpp')), '-o', str(output / 'check')]
    subprocess.run(command, check=True)
    run = [str(output / 'check')]
    if args.animations:
        run += [str(args.animations), args.mode]
    subprocess.run(run, check=True)
