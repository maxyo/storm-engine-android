#!/usr/bin/env python3
"""Run the util case-insensitive string testsuite under ASan/UBSan; no engine build."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
deps = a.dependency_sources.resolve()
with tempfile.TemporaryDirectory(prefix='storm-string-compare-') as temporary:
    binary = Path(temporary)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-sanitize-recover=all',
                    '-I'+str(root/'src/libs/util/include'), '-I'+str(deps/'catch2/single_include'),
                    str(root/'src/libs/util/testsuite/string_compare.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
