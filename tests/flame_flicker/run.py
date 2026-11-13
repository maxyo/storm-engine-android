#!/usr/bin/env python3
"""The flames' flicker (ship lanterns, location lights) looks the same at every time scale and stops in a pause
(src/libs/ship/src/flame_flicker.hpp, src/libs/location/src/flame_flicker.hpp, storm/cosmetic_time.hpp)."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # keep the source tree clean
sys.path.insert(0, str(root / 'tests/glm_math'))
from glm_source import math_include_flags
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='storm-flame-flicker-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([*os.environ.get('CXX', 'c++').split(), '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'src/libs/core/include'), '-I' + str(root / 'src/libs'),
                    *math_include_flags(args.glm_source),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
