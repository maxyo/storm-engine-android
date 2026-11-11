#!/usr/bin/env python3
"""Check the interface picture lists (src/libs/xinterface/src/picture_catalog.cpp): which files each
game profile reads, picture line parsing and the duplicate-group / malformed-line errors."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-xi-pictures-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src/libs/core/include'), '-I' + str(root / 'src/libs/xinterface/src'),
                    str(Path(__file__).with_name('check.cpp')),
                    str(root / 'src/libs/xinterface/src/picture_catalog.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
