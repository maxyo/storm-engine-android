#!/usr/bin/env python3
"""Host check: every grass map (.grs) of a game packs into the grass vertices (src/libs/location/src/grass_pack.hpp).

  run.py <game or RESOURCE directory>...   the game data is not in the repository; pass the installed game"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) < 2:
    raise SystemExit(__doc__)
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-grass-pack-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src/libs/location/src'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary), *sys.argv[1:]], check=True)
