#!/usr/bin/env python3
"""Run host storage/lifecycle tests, or compile them with CXX for Android."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
p.add_argument('--compile-only', action='store_true')
a = p.parse_args()
r = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-runtime-') as d:
    b = Path(d)
    (b / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
    target = b / ('check.o' if a.compile_only else 'check')
    cmd = [os.environ.get('CXX', 'c++'), '-std=c++20', '-I' + str(b),
           '-I' + str(r / 'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
           '-o', str(target)]
    if a.compile_only:
        cmd.append('-c')
    subprocess.run(cmd, check=True)
    if not a.compile_only:
        subprocess.run([str(target)], check=True)
    print('PASS: storage/lifecycle ' + ('compiled' if a.compile_only else 'executed'))
