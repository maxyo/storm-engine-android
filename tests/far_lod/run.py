#!/usr/bin/env python3
"""Build and run the far LOD host check (src/libs/geometry/far_lod; check.cpp and verify.hpp next to this file) on
synthetic models (seconds). The game's models: tools/game_checks/far_lod. Uses the pinned meshoptimizer and the Storm
ccache launcher; the kept build directory keeps reruns incremental.
"""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, required=True)
a = p.parse_args()
here = Path(__file__).resolve().parent
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
subprocess.run(['cmake', '-S', str(here), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'], check=True,
               stdout=subprocess.DEVNULL)
subprocess.run(['cmake', '--build', str(build), '--target', 'far_lod_check'], check=True)
raise SystemExit(subprocess.run([str(build / 'far_lod_check')]).returncode)
