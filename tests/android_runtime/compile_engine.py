#!/usr/bin/env python3
"""Compile real Android engine entry point and file service; does not link engine."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('ndk', 'sdl-source', 'spdlog-source', 'fmt-source', 'mimalloc-source', 'dxvk-source'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
r = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-entry-') as d:
    b = Path(d)
    (b / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
    watermark = b / 'generated_revision.hpp'
    watermark.write_text('#define STORM_BUILD_WATERMARK android_compile_check\n')
    includes = list((r / 'src/libs').glob('*/include')) + [r / 'src/libs/core/src', b]
    includes += [a.spdlog_source / 'include', a.fmt_source / 'include', a.mimalloc_source / 'include',
                 a.dxvk_source / 'include/native/directx', a.dxvk_source / 'include/native/windows']
    compiler = a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++'
    for source in ('src/apps/engine/src/main.cpp', 'src/libs/core/src/file_service.cpp'):
        command = [str(compiler), '-std=c++20', '-DSPDLOG_FMT_EXTERNAL', '-DFMT_HEADER_ONLY',
                   '-DSTORM_WATERMARK_FILE="' + str(watermark) + '"']
        command += ['-I' + str(x.resolve()) for x in includes]
        subprocess.run(command + ['-c', str(r / source), '-o', str(b / (Path(source).stem + '.o'))], check=True)
        print('PASS: real ARM64 object ' + source)
