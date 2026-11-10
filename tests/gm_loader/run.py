#!/usr/bin/env python3
"""Load synthetic .gm 1.05 / 1.06 files with the production geometry loader (geom_static.cpp,
collide.cpp) through a fake GEOM_SERVICE, and reject other versions.

Optional (local only): --game-dir <Resource directory> loads every 1.05 / 1.06 .gm below it and
checks that the loader consumes each file exactly to its end.
"""
import argparse
import os
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--glm-source', type=Path, required=True)
p.add_argument('--fmt-source', type=Path, required=True)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--game-dir', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
geometry = root / 'src/libs/geometry'
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
compiler = [os.environ.get('CXX', 'c++')]  # run_host_tests.py sets CXX=tools/ccache/cxx
flags = ['-std=c++20', '-O1', '-DFMT_HEADER_ONLY', '-I' + str(geometry / 'src'), '-I' + str(geometry / 'include'),
         '-I' + str(root / 'src/libs/math/include'), '-I' + str(root / 'src/libs/core/include'),
         '-I' + str(a.glm_source), '-I' + str(a.fmt_source / 'include')]
started = time.monotonic()
objects = []
for source in (Path(__file__).with_name('check.cpp'), geometry / 'src/geom_static.cpp', geometry / 'src/collide.cpp'):
    objects.append(build / (source.stem + '.o'))
    subprocess.run(compiler + flags + ['-c', str(source), '-o', str(objects[-1])], check=True)
binary = build / 'check'
subprocess.run([os.environ.get('CXX', 'c++')] + [str(o) for o in objects] + ['-o', str(binary)], check=True)
print(f'built in {time.monotonic() - started:.1f}s', flush=True)
subprocess.run([str(binary)] + ([str(a.game_dir.resolve())] if a.game_dir else []), check=True)
