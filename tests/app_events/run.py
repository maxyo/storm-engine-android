#!/usr/bin/env python3
"""Exercise AppEvents (window/lifecycle hub) with the real host SDL library."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
p.add_argument('--sdl-library', default='libSDL2-2.0.so.0')
p.add_argument('--ndk', type=Path)
a = p.parse_args()
r = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-app-events-') as d:
    b = Path(d)
    (b / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
    flags = ['-std=c++20', '-pthread', '-I' + str(b), '-I' + str(r / 'src/libs/window/include')]
    source = r / 'src/libs/window/src/app_events.cpp'
    for sanitizer in ('address,undefined', 'thread'):
        binary = b / ('check-' + sanitizer.split(',')[0])
        subprocess.run([os.environ.get('CXX', 'c++'), *flags, '-g', '-fsanitize=' + sanitizer,
                        '-fno-omit-frame-pointer', str(Path(__file__).with_name('check.cpp')), str(source),
                        '-Wl,-l:' + a.sdl_library, '-o', str(binary)], check=True)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', TSAN_OPTIONS='ignore_noninstrumented_modules=1')
        subprocess.run([str(binary)], env=env, check=True, timeout=120)
    print('PASS: AppEvents translation, game-thread delivery, early delivery, coalescing, bounds', flush=True)
    if a.ndk:
        compiler = a.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++'
        subprocess.run([str(compiler), *flags, '-c', str(source), '-o', str(b / 'app_events.o')], check=True)
        print('PASS: AppEvents ARM64 translation unit', flush=True)
