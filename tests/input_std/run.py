#!/usr/bin/env python3
"""Real SDL adapter + compiler string-list tests; never touches engine builds/devices.

Uses an installed host SDL2 .so and existing official SDL headers, plus optional
NDK compile checks. Run under xvfb-run -a for isolated relative mouse capture.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
p.add_argument('--dxvk-source', type=Path, required=True)
p.add_argument('--sdl-library', default='libSDL2-2.0.so.0')
p.add_argument('--ndk', type=Path)
p.add_argument('--dependency-sources', type=Path, help='Existing fmt/spdlog/mimalloc source parent, for real engine TU compilation')
a = p.parse_args()
r = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-input-std-') as d:
    b = Path(d)
    (b / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
    includes = [b, a.sdl_source.resolve()/'include', r/'src/libs/input/include', r/'src/libs/input/src', r/'src/libs/core/src',
                r/'src/libs/util/include', a.dxvk_source.resolve()/'include/native/directx',
                a.dxvk_source.resolve()/'include/native/windows']
    flags = ['-std=c++20', '-pthread'] + ['-I'+str(i) for i in includes]
    cxx = os.environ.get('CXX', 'c++')
    # Leak/UB checks cover repeated segment release and erase/append ownership.
    strings = b/'strings'
    subprocess.run([cxx, *flags, '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                    str(r/'tests/input_std/strings.cpp'), str(r/'src/libs/core/src/strings_list.cpp'),
                    '-o', str(strings)], check=True)
    subprocess.run([str(strings)], check=True)
    print('PASS: strings storage ASan/UBSan (growth, pointer stability, order, payload, release)', flush=True)
    for android_queue in (False, True):
        target = b/('android-queue' if android_queue else 'desktop')
        command = [cxx, *flags]
        if android_queue:
            command += ['-D__ANDROID__', '-DSTORM_INPUT_HOST_TEST']
        command += [str(r/'tests/input_std/input.cpp'), str(r/'src/libs/input/src/sdl_input.cpp'),
                    '-Wl,-l:'+a.sdl_library, '-o', str(target)]
        subprocess.run(command, check=True)
        subprocess.run([str(target)], check=True)
        print('PASS: real SDL host runtime, Android deferral branch=' + str(android_queue), flush=True)
    if a.ndk:
        compiler = a.ndk.resolve()/'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++'
        flags += ['-I'+str(i) for i in (r/'src/libs').glob('*/include')]
        if not a.dependency_sources:
            p.error('--ndk requires --dependency-sources to compile real PCS_CONTROLS/compiler consumers')
        for dep in ('fmt', 'spdlog', 'mimalloc', 'fast_float'):
            flags += ['-I'+str(a.dependency_sources.resolve()/dep/'include')]
        flags += ['-DSPDLOG_FMT_EXTERNAL', '-DFMT_HEADER_ONLY']
        for source in ('src/libs/input/src/sdl_input.cpp', 'src/libs/pcs_controls/src/pcs_controls.cpp',
                       'src/libs/core/src/strings_list.cpp', 'src/libs/core/src/compiler.cpp'):
            subprocess.run([str(compiler), *flags, '-c', str(r/source), '-o', str(b/(Path(source).stem+'.o'))], check=True)
            print('PASS: real Android ARM64 translation unit: ' + source, flush=True)
