#!/usr/bin/env python3
"""World-map labels that collide on the screen: the heavier shows (the game's weights), deterministically, with no flicker."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-worldmap-labels-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/worldmap/src'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: apart labels all show; colliding ones yield to the heavier (equal weights: the game's order); a hidden label takes "
      "no room; the gap is kept; a jittering camera at the gap's edge does not swap them; the result is deterministic")
