#!/usr/bin/env python3
"""Run the actual semantic combat mailbox/state with ASan and UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-combat-') as directory:
    binary=Path(directory)/'check'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fsanitize=address,undefined','-I'+str(root/'src/libs/util/include'),str(Path(__file__).with_name('check.cpp')),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
