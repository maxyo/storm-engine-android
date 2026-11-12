#!/usr/bin/env python3
"""Host checks for the engine -> host OS hooks (src/libs/util/include/platform/host_hooks.hpp).

With --sdl-source and CXX set to the NDK clang++, only compiles the checks for Android,
which also compiles the JNI frame work hint (report and target)."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-host-hooks-') as directory:
    build = Path(directory)
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
               '-I' + str(root / 'src/libs/util/include'), str(Path(__file__).with_name('check.cpp'))]
    if a.sdl_source:
        (build / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
        subprocess.run(command + ['-I' + str(build), '-c', '-o', str(build / 'check.o')], check=True)
        print('PASS: host hooks compiled for the target')
    else:
        binary = build / 'check'
        subprocess.run(command + ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-pthread',
                                  '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
