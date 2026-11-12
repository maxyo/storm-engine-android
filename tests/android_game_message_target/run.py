#!/usr/bin/env python3
"""Where a game log line's toast leads, told by the script function that wrote it."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-message-target-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: level/ability lines lead to the abilities, quest records to that quest, taken items to the inventory, the rest nowhere; the innermost writer decides')
