#!/usr/bin/env python3
"""Bit-exact parity of the SIMD skinning kernel with the scalar reference.

Host: scalar vs SSE2 instantiation of the shared four-lane kernel, under
ASan/UBSan, with exact-size buffers. With --glm-source (pinned GLM 1.0.3
checkout, e.g. <build>/_deps/storm_glm-src) the scalar reference is also
compared with the previous GLM matrix-product implementation.
With --ndk the NEON instantiation is compiled for arm64 (not executed: the
host cannot run arm64 code; run the produced binary on a device to repeat the
parity check there: `check` prints PASS and the timings).
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--glm-source', type=Path)
p.add_argument('--ndk', type=Path)
p.add_argument('--arm64-output', type=Path, help='keep the arm64 test binary here')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
source = Path(__file__).with_name('check.cpp')
flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src/libs/model/src')]
if a.glm_source:
    flags += ['-DSTORM_SKINNING_LEGACY_GLM', '-I' + str(a.glm_source.resolve()),
              '-I' + str(root / 'src/libs/math/include'), '-I' + str(root / 'src/libs/util/include')]
with tempfile.TemporaryDirectory(prefix='storm-skinning-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    fast = Path(directory) / 'check-fast'
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, str(source), '-o', str(fast)], check=True)
    subprocess.run([str(fast), '--timing'], check=True)
    if a.ndk:
        compiler = a.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++'
        output = a.arm64_output.resolve() if a.arm64_output else Path(directory) / 'check-arm64'
        subprocess.run([str(compiler), *flags, '-static-libstdc++', str(source), '-o', str(output)], check=True)
        print('PASS: NEON kernel and parity test compiled for arm64 (not executed on this host)', flush=True)
