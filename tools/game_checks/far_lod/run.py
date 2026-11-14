#!/usr/bin/env python3
"""Game-data check: every model of an installed game that gets far LODs (islands, ships, outdoor locations:
far_lod::Wanted) is built and checked (src/libs/geometry/far_lod; what is checked: tests/far_lod/verify.hpp), with the
triangles kept per level, the encoded size and the build time by folder. Needs the game, so it is not a host check
(tests/far_lod checks synthetic models); run it before a release, like the scene sweep. Builds in build/game-checks.

  run.py <game directory>"""
from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit(__doc__)
here = Path(__file__).resolve().parent
root = here.parents[2]
includes = ['-I' + str(root / 'tests/far_lod'), '-I' + str(root / 'src/libs/geometry/far_lod')]
game = Path(sys.argv[1]).resolve()
models = next((p for p in (game / 'RESOURCE/MODELS', game / 'resource/models', game / 'MODELS') if p.is_dir()), None)
if models is None:
    raise SystemExit(f'{game}: no RESOURCE/MODELS folder')
build = root / 'build/game-checks/far_lod'
build.mkdir(parents=True, exist_ok=True)
subprocess.run(['cmake', '-S', str(here), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                '-DFAR_LOD_INCLUDES=' + ';'.join(i[2:] for i in includes)], check=True, stdout=subprocess.DEVNULL)
subprocess.run(['cmake', '--build', str(build), '--target', 'far_lod_game_check'], check=True)
raise SystemExit(subprocess.run([str(build / 'far_lod_game_check'), str(models)]).returncode)
