#!/usr/bin/env python3
"""Host checks for the frame pacing's swap interval choice (src/libs/renderer_bgfx/src/shim/swap_interval.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-frame-pacing-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(root / 'src/libs/renderer_bgfx/src'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
