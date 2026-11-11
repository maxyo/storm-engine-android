#!/usr/bin/env python3
"""Check the hover trigger state machine of the TRIGGER node (src/libs/xinterface/src/hover_trigger.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-hover-trigger-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src/libs/xinterface/src'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
