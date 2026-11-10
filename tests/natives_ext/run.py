#!/usr/bin/env python3
"""Check the engine-independent parts of the Caribbean Legend internal natives (src/libs/core/src/natives_logic.*)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
core = root / 'src/libs/core/src'
with tempfile.TemporaryDirectory(prefix='storm-natives-ext-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(core),
                    str(Path(__file__).with_name('check.cpp')), str(core / 'natives_logic.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
