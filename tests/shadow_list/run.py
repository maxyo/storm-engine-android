#!/usr/bin/env python3
"""Shadow::prepareAll walks the shadow list while shadows are created: index walk, ASan/UBSan (live_list.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='storm-shadow-list-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([*compiler.split(), '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(root / 'src/libs/shadow/src'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
