#!/usr/bin/env python3
"""Ship-island contact damage power: src/libs/touch/src/island_impact.hpp (TOUCH, SHIP_SHIP2ISLAND_COLLISION), ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='storm-island-impact-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([*compiler.split(), '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(root / 'src/libs/touch/src'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
