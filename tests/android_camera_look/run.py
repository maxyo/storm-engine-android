#!/usr/bin/env python3
"""The world map's camera moved by a finger: the turn (1:1 drag, the fling on the frame clock, the angle per travel) and
the free camera's pan (the ground under the finger stays under it at every height)."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/glm_math'))
from glm_source import math_include_flags  # noqa: E402
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='storm-camera-look-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), *math_include_flags(args.glm_source),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: a drag turns by the finger's travel, the same at 30 and 60 fps while down and after the release; the release coasts on "
      "at the finger's speed (no jump) and rests; a finger stops a coast, a cancel stops at once, a rested finger does not coast; "
      "bad commands refused; the near ground follows the turning finger 1:1 at every height; the free camera's pan keeps the "
      "ground under the finger at heights 50, 300 and 500 in any direction, frame by frame, and coasts on along the drag")
