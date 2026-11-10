#!/usr/bin/env python3
"""Build the bgfx renderer library (shim + embedded shaders) and run its texture transcode tests.

Uses the pinned bgfx (src/libs/renderer_bgfx/cmake/bgfx_pin.cmake) and etcpak. Fast paths, as in CI:
STORM_SHADERC=<host shaderc from src/libs/renderer_bgfx/host-tools> skips building bgfx's tools, and
ccache (when installed) plus the kept build directory make repeated runs incremental. Offline: set
FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM / FETCHCONTENT_SOURCE_DIR_STORM_ETCPAK in the environment.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
p.add_argument('--build-dir', type=Path, required=True)
a = p.parse_args()
here = Path(__file__).resolve().parent
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
sdl = build / 'sdl-include'
sdl.mkdir(exist_ok=True)
if not (sdl / 'SDL2').exists():
    (sdl / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
configure = ['cmake', '-S', str(here), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             f'-DSTORM_SDL_INCLUDE={sdl}']
if shutil.which('ccache'):
    configure += ['-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache']
for name in ('STORM_SHADERC', 'FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM', 'FETCHCONTENT_SOURCE_DIR_STORM_ETCPAK'):
    if os.environ.get(name):
        configure.append(f'-D{name}={os.environ[name]}')
subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
subprocess.run(['cmake', '--build', str(build), '--target', 'storm_bgfx', 'storm_texture_tests'], check=True)
subprocess.run([str(build / 'renderer_bgfx' / 'storm_texture_tests')], check=True)
print('PASS: storm_bgfx builds; storm_texture_tests', flush=True)
