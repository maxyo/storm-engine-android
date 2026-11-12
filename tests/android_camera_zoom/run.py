#!/usr/bin/env python3
"""The pinch of the camera look: the mailbox of a frame and the mapping to each camera's zoom value."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-camera-zoom-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: pinch factors multiply within a frame, act only in it (a dropped one is reported once, never applied later), "
      "bad commands refused; ship distance, world-map height, spyglass magnification and land radius follow the fingers within the game's limits; deck FOV zoom 1x..2x eases to the fingers and resets off the deck; a raised spyglass grows in from the zoomed deck view and gives it back")
