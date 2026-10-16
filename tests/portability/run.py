#!/usr/bin/env python3
"""x86 host oracle tests for native SSE and the portable scalar code paths."""
import os
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="storm-portability-") as directory:
    command = ['cmake', '-S', str(Path(__file__).parent), '-B', directory,
               '-DCMAKE_CXX_COMPILER=' + os.environ.get('CXX', 'c++')]
    if args.glm_source:
        command.append('-DFETCHCONTENT_SOURCE_DIR_STORM_GLM=' + str(args.glm_source.resolve()))
    subprocess.run(command, check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    subprocess.run(['ctest', '--test-dir', directory, '--output-on-failure'], check=True)
