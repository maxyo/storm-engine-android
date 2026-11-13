#!/usr/bin/env python3
"""Autosave rotation: three records, the oldest replaced, the player's records never."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
xinterface = (root / 'src/libs/xinterface/src/xinterface.cpp').read_text()
# The engine answers the script from the same rotation and keeps the names from the player.
body = lambda start: xinterface[xinterface.index(start):xinterface.index('\n}', xinterface.index(start))]
assert 'AutosaveRotation' in body('std::string XINTERFACE::NextAutosaveRecord('), 'the engine does not rotate the autosave records'
assert 'AutosaveRecordNumber(fileName) != 0' in body('bool XINTERFACE::NewSaveFileName('), \
    "a record of the player may take an autosave record's name"
with tempfile.TemporaryDirectory(prefix='storm-save-autosave-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(root / 'src/libs/xinterface/src'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
