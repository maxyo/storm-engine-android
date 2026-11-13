#!/usr/bin/env python3
"""MODELR::Trace's skinned-geometry ray test on a two-object model (src/libs/model/src/skinned_trace.hpp)."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--glm-source', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-model-trace-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'src/libs/model/src'),
                    '-I' + str(root / 'src/libs/math/include'), '-I' + str(a.glm_source.resolve()),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
