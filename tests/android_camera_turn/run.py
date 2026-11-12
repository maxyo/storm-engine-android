#!/usr/bin/env python3
"""The world map's camera turned by a finger: 1:1 drag, the fling on the frame clock, the angle per travel."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-camera-turn-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: a drag turns by the finger's travel, the same at 30 and 60 fps while down and after the release; the release coasts on "
      "at the finger's speed (no jump) and rests; a finger stops a coast, a cancel stops at once, a rested finger does not coast; "
      "bad commands refused; the near ground follows the finger 1:1 at every height")
