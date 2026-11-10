#!/usr/bin/env python3
"""Build and run the PBR package checks (src/libs/renderer_bgfx/pbr).

  pbr_core_check    always: material resolution, IBL bake, BRDF table, preset constants (synthetic, no GPU)
  pbr_shim_check    always: the shim's PBR hook on bgfx's Noop backend (program choice, skip without lighting)
  pbr_render_check  with --render: headless GLES 3.0 (EGL) render of synthetic spheres and, with --game, of one
                    Caribbean Legend model with its sidecar; images go to --out (keep it outside the repository,
                    game images are local only)

Uses the pinned bgfx, the shared host shaderc and the Storm ccache launcher, like tests/renderer_bgfx/run.py.
"""
import argparse
import os
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--render', action='store_true')
p.add_argument('--fmt-source', type=Path, help='fmt source tree (with --render)')
p.add_argument('--game', type=Path, help='Caribbean Legend game directory (read-only)')
p.add_argument('--model', default='object/Globe_01', help='model path below Resource/Models, no extension')
p.add_argument('--preset', default='BaseTown.json')
p.add_argument('--hour', type=int, default=12)
p.add_argument('--texture-path', default='', help='prefix of the model\'s own texture names (scripts: GEOMETRY::SetTexturePath)')
p.add_argument('--out', type=Path)
a = p.parse_args()
here = Path(__file__).resolve().parent
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
sdl = build / 'sdl-include'
sdl.mkdir(exist_ok=True)
if not (sdl / 'SDL2').exists():
    (sdl / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
started = time.monotonic()
configure = ['cmake', '-S', str(here), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DSTORM_SDL_INCLUDE={sdl}']
if a.render:
    if not a.fmt_source:
        p.error('--render needs --fmt-source')
    configure.append(f'-DSTORM_FMT_SOURCE={a.fmt_source.resolve()}')
for name in ('STORM_SHADERC', 'FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM', 'FETCHCONTENT_SOURCE_DIR_STORM_ETCPAK',
             'FETCHCONTENT_SOURCE_DIR_STORM_JSON'):
    if os.environ.get(name):
        configure.append(f'-D{name}={os.environ[name]}')
subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
targets = ['pbr_core_check', 'pbr_shim_check'] + (['pbr_render_check'] if a.render else [])
warmth = here.parents[1] / 'tools/ccache/warmth'
subprocess.run([str(warmth), 'begin', str(build)], check=True)
subprocess.run(['cmake', '--build', str(build), '--target', *targets], check=True)
subprocess.run([str(warmth), 'end', str(build)], check=True)
print(f'built in {time.monotonic() - started:.1f}s', flush=True)
t = time.monotonic()
subprocess.run([str(build / 'renderer_bgfx' / 'pbr' / 'pbr_core_check')], check=True)
print(f'pbr_core_check {time.monotonic() - t:.2f}s', flush=True)
t = time.monotonic()
subprocess.run([str(build / 'pbr_shim_check')], check=True)
print(f'pbr_shim_check {time.monotonic() - t:.2f}s', flush=True)
if a.render:
    if not a.out:
        p.error('--render needs --out')
    a.out.mkdir(parents=True, exist_ok=True)
    cmd = [str(build / 'pbr_render_check'), str(a.out.resolve())]
    if a.game:
        cmd += ['--game', str(a.game.resolve()), '--model', a.model, '--preset', a.preset, '--hour', str(a.hour), '--texture-path', a.texture_path]
    env = dict(os.environ)
    env.setdefault('EGL_PLATFORM', 'surfaceless')
    t = time.monotonic()
    subprocess.run(cmd, check=True, env=env)
    print(f'pbr_render_check {time.monotonic() - t:.2f}s', flush=True)
