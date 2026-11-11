#!/usr/bin/env python3
"""Check the ship's script message/event layouts with the real MESSAGE; no engine build or game data."""
from pathlib import Path
import argparse, os, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--glm-source', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
deps = a.dependency_sources.resolve()
includes = [root/'src/libs/core/include', root/'src/libs/math/include', root/'src/libs/util/include',
            root/'src/libs/shared_headers/include', root/'src/libs/ship/src', deps/'fmt/include',
            root/'src/libs/renderer_bgfx/include', a.glm_source.resolve()]
with tempfile.TemporaryDirectory(prefix='storm-ship-script-protocol-') as temporary:
    binary = Path(temporary)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-DFMT_HEADER_ONLY', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-sanitize-recover=all', *['-I'+str(i) for i in includes],
                    str(root/'src/libs/core/src/message.cpp'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
