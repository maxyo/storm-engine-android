#!/usr/bin/env python3
"""Game-data check: every grass map (.grs) of an installed game packs into the grass vertices
(src/libs/location/src/grass_pack.hpp; the map check is grass_maps.hpp). Needs the game, so it is not a host check
(tests/grass_pack checks the packing on synthetic maps); run it before a release, like the scene sweep.

  run.py <game or RESOURCE directory>..."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) < 2:
    raise SystemExit(__doc__)
here = Path(__file__).resolve().parent
root = here.parents[2]
with tempfile.TemporaryDirectory(prefix='storm-grass-pack-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src/libs/location/src'), str(here / 'check.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), *sys.argv[1:]], check=True)
