#!/usr/bin/env python3
"""A model's location lights depend on where the model is, never on the camera, and change without a jump (light_choice.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='storm-light-choice-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([*compiler.split(), '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(root / 'src/libs/location/src'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
