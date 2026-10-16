#!/usr/bin/env python3
"""Build math contracts, optionally compare exactly the same harness against a Git revision."""
import argparse
from array import array
import io
import math
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
parser.add_argument('--baseline', help='Optional engine Git revision for differential checks')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--aligned-defaults', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
build = (args.build_dir or root / 'build/glm-tests').resolve()
build.mkdir(parents=True, exist_ok=True)

def run_variant(name, baseline_headers=None):
    target = build / name
    command = ['cmake', '-S', str(Path(__file__).parent), '-B', str(target),
               '-DCMAKE_BUILD_TYPE=Release', '-DSTORM_MATH_SANITIZE=' + ('ON' if args.sanitize else 'OFF')]
    if args.glm_source:
        command.append('-DFETCHCONTENT_SOURCE_DIR_STORM_GLM=' + str(args.glm_source.resolve()))
    if baseline_headers:
        command.append('-DSTORM_MATH_BASELINE_HEADERS=' + str(baseline_headers))
    if args.aligned_defaults:
        command.append('-DCMAKE_CXX_FLAGS=-DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES -DGLM_FORCE_INTRINSICS')
    subprocess.run(command, check=True)
    subprocess.run(['cmake', '--build', str(target), '--parallel', '2'], check=True)
    subprocess.run([str(target / 'packed_vector_include')], check=True)
    samples = {}
    for executable in ('check', 'packed_check'):
        samples[executable] = target / (executable + '-samples.bin')
        subprocess.run([str(target / executable), str(samples[executable])], check=True)
    return samples

current = run_variant('current')
if args.baseline:
    archive = subprocess.check_output(['git', '-C', str(root), 'archive', args.baseline,
                                      'src/libs/math/include'])
    original = build / 'baseline-source'
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(original, filter='data')
    previous = run_variant('baseline', original / 'src/libs/math/include')
    for executable in current:
        new, old = array('d'), array('d')
        new.frombytes(current[executable].read_bytes()); old.frombytes(previous[executable].read_bytes())
        if len(new) != len(old):
            raise RuntimeError('Differential sample count changed')
        largest = 0.0
        for index, (actual, expected) in enumerate(zip(new, old)):
            if actual == expected or (math.isnan(actual) and math.isnan(expected)):
                continue
            error = abs(actual - expected) / (1 + abs(expected))
            if not math.isfinite(error) or error > 5e-5:
                raise RuntimeError(f'Legacy mismatch at {index}: {actual} vs {expected}')
            largest = max(largest, error)
        print(f'PASS {executable}: {len(new)} scalar differential samples vs {args.baseline}; max scaled error={largest:.3g}')
