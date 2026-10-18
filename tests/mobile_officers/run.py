#!/usr/bin/env python3
"""Host ASan/UBSan check for the production officer selection capability."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-officers-') as tmp:
    binary = str(Path(tmp) / 'selection')
    subprocess.run(['c++', '-std=c++20', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I'+str(root/'src/libs/xinterface/src'),
                    str(Path(__file__).with_name('selection.cpp')), '-o', binary], check=True)
    subprocess.run([binary], check=True)
