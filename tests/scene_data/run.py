#!/usr/bin/env python3
"""Build the scene_data JSON parsers and check them on synthetic files.

Optional (local only): --game-dir <Resource directory> parses every model sidecar, lighting
preset, texture_library.json and Effects/effects.json of a Caribbean Legend install and prints
the counts. Offline: set FETCHCONTENT_SOURCE_DIR_STORM_JSON to an extracted nlohmann/json 3.12.0.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--game-dir', type=Path)
a = p.parse_args()
here = Path(__file__).resolve().parent
build = a.build_dir.resolve()
started = time.monotonic()
configure = ['cmake', '-S', str(here), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release']
if shutil.which('ccache'):
    configure += ['-DCMAKE_CXX_COMPILER_LAUNCHER=ccache']
if os.environ.get('FETCHCONTENT_SOURCE_DIR_STORM_JSON'):
    configure.append(f'-DFETCHCONTENT_SOURCE_DIR_STORM_JSON={os.environ["FETCHCONTENT_SOURCE_DIR_STORM_JSON"]}')
subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
subprocess.run(['cmake', '--build', str(build), '--target', 'scene_data_check'], check=True, stdout=subprocess.DEVNULL)
print(f'built in {time.monotonic() - started:.1f}s', flush=True)
subprocess.run([str(build / 'scene_data_check')] + ([str(a.game_dir.resolve())] if a.game_dir else []), check=True)
