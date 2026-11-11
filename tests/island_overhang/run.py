#!/usr/bin/env python3
"""Ships vs. island overhangs: storm::island::OverheadContact (src/libs/island/include/island_overhang.hpp), ASan/UBSan.

The engine uses it where a hull meets the island: TOUCH (ship-island contact, the grounding stop and its damage),
ISLAND::Check2DBoxDepth (AI placement) and the AI touch rays. Usage: run.py --glm-source /path/to/glm.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', required=True, type=Path)
args = parser.parse_args()
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='storm-island-overhang-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([*compiler.split(), '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(root / 'src/libs/island/include'),
                    '-I' + str(root / 'src/libs/math/include'), '-isystem', str(args.glm_source),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
