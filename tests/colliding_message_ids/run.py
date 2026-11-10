#!/usr/bin/env python3
"""Check the TEHO / Caribbean Legend colliding message ids (src/libs/core/include/colliding_message_ids.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-colliding-ids-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src/libs/core/include'), '-I' + str(root / 'src/libs/shared_headers/include'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
