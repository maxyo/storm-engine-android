#!/usr/bin/env python3
"""Spyglass state channel and coverage acknowledgement."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-spyglass-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: spyglass state JSON (hidden fields left out, escaping, revisions), captain line, coverage acknowledgement by generation and age")
