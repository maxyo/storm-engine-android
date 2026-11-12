#!/usr/bin/env python3
"""Build the bgfx renderer library (shim + embedded shaders), run its texture transcode tests and
the draw/state counter check of a fixed synthetic frame (perf_check.cpp, bgfx Noop backend) and the
GPU skinning parity check against the CPU kernel (skinning_check.cpp, OpenGL ES in an SDL window: needs an X display, e.g. xvfb-run).

Uses the pinned bgfx (src/libs/renderer_bgfx/cmake/bgfx_pin.cmake) and etcpak, the shared host shaderc
of that pin (STORM_SHADERC overrides it) and the Storm ccache launcher (tools/ccache/README.md); the kept
build directory makes repeated runs incremental. Offline: set FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM /
FETCHCONTENT_SOURCE_DIR_STORM_ETCPAK in the environment.
"""
import argparse
import os
from pathlib import Path
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
for name in ('STORM_SHADERC', 'FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM', 'FETCHCONTENT_SOURCE_DIR_STORM_ETCPAK'):
    if os.environ.get(name):
        configure.append(f'-D{name}={os.environ[name]}')
# Each vertex port declares the constant registers it reads (PORT_VSC_COUNT); the shim uploads the count
# of its ports table (src/shim/ports.cpp). They must agree and cover every register the port reads.
import re
shim = here.parents[1] / 'src/libs/renderer_bgfx'
table = dict(re.findall(r'"(vs_port_\w+)", 0, (\d+)\}', (shim / 'src/shim/ports.cpp').read_text()))
for source in sorted((shim / 'shaders/ports').glob('vs_port_*.sc')):
    text = source.read_text()
    declared = int(re.search(r'#define PORT_VSC_COUNT (\d+)', text)[1])
    used = max(int(i) for i in re.findall(r'u_vsc\[(\d+)\]', text)) + 1
    if declared < used or int(table.get(source.stem, -1)) != declared:
        raise SystemExit(f'{source.name}: PORT_VSC_COUNT {declared}, highest constant register used {used - 1}, '
                         f'ports.cpp vsc_count {table.get(source.stem)}')
subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
warmth = here.parents[1] / 'tools/ccache/warmth'
subprocess.run([str(warmth), 'begin', str(build)], check=True)
subprocess.run(['cmake', '--build', str(build), '--target', 'storm_bgfx', 'storm_texture_tests', 'perf_check', 'skinning_check',
                'preset_switch_check', 'program_cache_check', 'storm_fx_core_check', 'ao_volume_check'], check=True)
subprocess.run([str(warmth), 'end', str(build)], check=True)
subprocess.run([str(build / 'renderer_bgfx' / 'storm_texture_tests')], check=True)
subprocess.run([str(build / 'perf_check')], check=True)
subprocess.run([str(build / 'skinning_check')], check=True)
subprocess.run([str(build / 'preset_switch_check')], check=True)
subprocess.run([str(build / 'program_cache_check')], check=True)
subprocess.run([str(build / 'renderer_bgfx' / 'fx' / 'storm_fx_core_check')], check=True)
subprocess.run([str(build / 'ao_volume_check')], check=True)
print('PASS: storm_bgfx builds; storm_texture_tests; perf_check; skinning_check; preset_switch_check; program_cache_check; '
      'fx core_check; ao_volume_check',
      flush=True)
