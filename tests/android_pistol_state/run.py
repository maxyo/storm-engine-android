#!/usr/bin/env python3
"""Check observational pistol state without invoking game/script events."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-pistol-') as directory:
    binary=Path(directory)/'check'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'src/libs/util/include'),str(Path(__file__).with_name('check.cpp')),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    print('PASS missing/invalid observations, unloaded threshold, quantized charge, partial multi-shot readiness')
