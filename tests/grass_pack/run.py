#!/usr/bin/env python3
"""Host check of the grass vertices (src/libs/location/src/grass_pack.hpp) on synthetic grass maps, no game data
(every map of an installed game: tools/game_checks/grass_pack)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-grass-pack-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(root / 'src/libs/location/src'), '-I' + str(root / 'tools/game_checks/grass_pack'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
