#!/usr/bin/env python3
"""Island zipped maps made when the game does not ship them: storm::island::ZapCachePath (src/libs/island/include/island_zap.hpp)
puts them under the cache root, never into the game folder. ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='storm-island-zap-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([*compiler.split(), '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'src/libs/island/include'), str(Path(__file__).with_name('check.cpp')), '-o', str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
