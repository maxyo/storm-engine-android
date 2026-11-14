#!/usr/bin/env python3
"""Build the bgfx renderer library (shim + embedded shaders), run its texture transcode tests and
the draw/state counter check of a fixed synthetic frame (perf_check.cpp, bgfx Noop backend) and the
GPU skinning parity check against the CPU kernel (skinning_check.cpp), the texture formats on the GPU
(texture_format_check.cpp) and the other checks that draw, in an SDL window (needs an X display, e.g. xvfb-run):
on OpenGL ES, then again on Vulkan where the host has a Vulkan device.

Uses the pinned bgfx (src/libs/renderer_bgfx/cmake/bgfx_pin.cmake), the shared host shaderc
of that pin (STORM_SHADERC overrides it) and the Storm ccache launcher (tools/ccache/README.md); the kept
build directory makes repeated runs incremental. Offline: set FETCHCONTENT_SOURCE_DIR_STORM_{BGFX,BX,BIMG,BGFX_CMAKE} in the
environment.
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
for name in ('STORM_SHADERC', 'FETCHCONTENT_SOURCE_DIR_STORM_BGFX', 'FETCHCONTENT_SOURCE_DIR_STORM_BX', 'FETCHCONTENT_SOURCE_DIR_STORM_BIMG',
             'FETCHCONTENT_SOURCE_DIR_STORM_BGFX_CMAKE'):
    if os.environ.get(name):
        configure.append(f'-D{name}={os.environ[name]}')
# Each vertex port declares the constant registers it reads (PORT_VSC_COUNT); the shim uploads the count
# of its ports table (src/shim/ports.cpp). They must agree and cover every register the port reads.
import re
shim = here.parents[1] / 'src/libs/renderer_bgfx'
table = dict(re.findall(r'"(vs_port_\w+)", 0, (\d+)[,}]', (shim / 'src/shim/ports.cpp').read_text()))
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
subprocess.run(['cmake', '--build', str(build), '--target', 'storm_bgfx', 'storm_texture_tests', 'perf_check', 'skinning_check', 'uniform_commit_check',
                'preset_switch_check', 'sea_edge_check', 'program_cache_check', 'render_thread_check',
                'storm_fx_core_check', 'ao_volume_check',
                'storm_relief_check', 'grass_vertex_check', 'cutout_caster_check', 'lamp_lifetime_check', 'relief_switch_check', 'texture_format_check', 'backend_select_check',
                'buffer_pool_check', 'thumbnail_check', 'pipeline_prewarm_check', 'wet_surfaces_check'], check=True)
subprocess.run([str(warmth), 'end', str(build)], check=True)
# Every check is its own process: they run in parallel (STORM_CHECK_JOBS, default 6), the draws on OpenGL ES
# and on Vulkan at once, longest first. A failing check's output is printed.
import concurrent.futures
jobs = int(os.environ.get('STORM_CHECK_JOBS', '6'))
plain = [[str(build / 'lamp_lifetime_check')], [str(build / 'renderer_bgfx' / 'storm_texture_tests')], [str(build / 'perf_check')], [str(build / 'backend_select_check')],
         [str(build / 'renderer_bgfx' / 'fx' / 'storm_fx_core_check')], [str(build / 'relief_switch_check')],
         [str(build / 'relief' / 'storm_relief_check'), (build / 'relief_models_path.txt').read_text()]]
# The checks that draw, on OpenGL ES.
gpu_checks = ['skinning_check', 'uniform_commit_check', 'preset_switch_check', 'sea_edge_check', 'program_cache_check',
              'render_thread_check', 'ao_volume_check', 'grass_vertex_check', 'cutout_caster_check', 'texture_format_check', 'buffer_pool_check',
              'thumbnail_check', 'pipeline_prewarm_check', 'wet_surfaces_check']
# The same checks on Vulkan (STORM_CHECK_BACKEND, check_backend.hpp), where the host has a Vulkan device (CI: Mesa's
# lavapipe). uniform_commit_check stays on OpenGL ES: it counts the GL backend's glUniform calls. Every run must
# say that it ran on Vulkan: a check that ended on OpenGL ES (the shim's logged exception for a Vulkan that does
# not start) fails here. Without any Vulkan device the pass is skipped, and said so.
vulkan_env = dict(os.environ, STORM_CHECK_BACKEND='vulkan')
vulkan_checks = [name for name in gpu_checks if name != 'uniform_commit_check']
SLOW = ('ao_volume_check', 'render_thread_check', 'program_cache_check', 'cutout_caster_check')


def run_check(command, env=None):
    return subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')


failures = []
with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
    order = sorted(gpu_checks, key=lambda name: name not in SLOW)
    vk = {name: pool.submit(run_check, [str(build / name)], vulkan_env) for name in order if name in vulkan_checks}
    gl = {name: pool.submit(run_check, [str(build / name)]) for name in order}
    other = [(command, pool.submit(run_check, command)) for command in plain]
    for command, future in other:
        if future.result().returncode != 0:
            failures.append((Path(command[0]).name, future.result()))
    for name in gpu_checks:
        if gl[name].result().returncode != 0:
            failures.append((name, gl[name].result()))
    vulkan = 'not run'
    for name in vulkan_checks:
        done = vk[name].result()
        choice = next((line for line in done.stdout.splitlines() if 'render backend: ' in line and '(render_backend = ' in line), '')
        if name == vulkan_checks[0] and 'render backend: OpenGL ES' in choice and re.search(
                r'no Vulkan loader|reports no physical device|vkCreateInstance failed|older than 1\.1', choice):
            vulkan = 'SKIPPED, this host has no Vulkan device: ' + choice.split('): ', 1)[-1]
            print(f'Vulkan checks {vulkan}', flush=True)
            break
        if done.returncode != 0 or 'render backend: Vulkan' not in choice:
            failures.append((f'{name} on Vulkan ({choice or "no render backend line"})', done))
            continue
        vulkan = choice.split('Vulkan device: ', 1)[-1]
for name, done in failures:
    print(f'--- {name}: exit {done.returncode}\n{done.stdout[-6000:]}', flush=True)
if failures:
    raise SystemExit('FAIL: ' + ', '.join(name for name, _ in failures))
print('PASS: storm_bgfx builds; storm_texture_tests; perf_check; backend_select_check; fx core_check; relief_switch_check; relief_check; '
      f'on OpenGL ES: {" ".join(gpu_checks)}; on Vulkan ({vulkan}): {" ".join(vulkan_checks)}', flush=True)
