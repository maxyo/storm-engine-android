#!/usr/bin/env python3
"""Run the production land commands mailbox/state contract with sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-land-commands-') as directory:
    binary = Path(directory) / 'check'
    for name in ('check.cpp', 'handoff.cpp', 'missing_source.cpp'):
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name(name)),
                    '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
