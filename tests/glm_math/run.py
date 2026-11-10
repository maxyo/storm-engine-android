#!/usr/bin/env python3
"""Build math contracts, optionally compare exactly the same harness against a Git revision."""
import argparse
from array import array
import io
import math
import struct
import sys
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
parser.add_argument('--baseline', help='Optional engine Git revision for differential checks')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--aligned-defaults', action='store_true')
parser.add_argument('--bench', action='store_true',
                    help='Also run the micro-benchmark of every built variant (never combined with --sanitize)')
parser.add_argument('--details', action='store_true',
                    help='Attribute non-bit-exact samples to harness source lines (harnesses that record them)')
parser.add_argument('--tolerance', type=float, default=5e-5,
                    help='Largest accepted |new-old|/(1+|old|) of a differential sample')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
build = (args.build_dir or root / 'build/glm-tests').resolve()
build.mkdir(parents=True, exist_ok=True)

HARNESSES = ('check', 'packed_check', 'builders_check', 'math3d_check')

def float_ordinal(value):
    """Position of the nearest float32 on the ordered float line (for ULP distances)."""
    try:
        bits = struct.unpack('<i', struct.pack('<f', value))[0]
    except OverflowError:
        return 0x7f800000 if value > 0 else -0x7f800000
    return bits if bits >= 0 else -(bits & 0x7fffffff)

def run_bench(target):
    rows = {}
    output = subprocess.check_output([str(target / 'bench')], text=True)
    for line in output.splitlines():
        fields = line.split()
        position = fields.index('ns/op')
        rows[' '.join(fields[:position - 1])] = (float(fields[position - 1]), fields[-1])
    return rows

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
    for executable in HARNESSES:
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
    failures = []
    for executable in current:
        new, old = array('d'), array('d')
        new.frombytes(current[executable].read_bytes()); old.frombytes(previous[executable].read_bytes())
        if len(new) != len(old):
            raise RuntimeError(f'{executable}: differential sample count changed ({len(new)} vs {len(old)})')
        # inexact: differing values; zero_sign: +0 vs -0; nan: NaN on exactly one side.
        largest, inexact, zero_sign, nan_mismatch, max_ulp, over, first = 0.0, 0, 0, 0, 0, 0, None
        lines, by_line = array('I'), {}
        where = Path(str(current[executable]) + '.lines')
        if args.details and where.exists():
            lines.frombytes(where.read_bytes())
        source = (Path(__file__).parent / (executable + '.cpp')).read_text().splitlines() if lines else []

        def attribute(index, ulp):
            if lines:
                entry = by_line.setdefault(lines[index], [0, 0])
                entry[0] += 1
                entry[1] = max(entry[1], ulp)
        for index, (actual, expected) in enumerate(zip(new, old)):
            if actual == expected:
                if actual == 0.0 and math.copysign(1.0, actual) != math.copysign(1.0, expected):
                    zero_sign += 1
                    attribute(index, 0)
                continue
            if math.isnan(actual) and math.isnan(expected):
                continue
            inexact += 1
            if math.isnan(actual) or math.isnan(expected):
                nan_mismatch += 1
                error = math.inf
                attribute(index, 1 << 32)
            else:
                ulp = abs(float_ordinal(actual) - float_ordinal(expected))
                max_ulp = max(max_ulp, ulp)
                attribute(index, ulp)
                error = abs(actual - expected) / (1 + abs(expected))
                if math.isfinite(error):
                    largest = max(largest, error)
            if not math.isfinite(error) or error > args.tolerance:
                over += 1
                if first is None:
                    first = f'sample {index}: {actual!r} vs {expected!r}'
        verdict = 'PASS' if not over else 'FAIL'
        print(f'{verdict} {executable}: {len(new)} scalar differential samples vs {args.baseline}; '
              f'non-bit-exact={inexact} (signed-zero only={zero_sign}, NaN-vs-number={nan_mismatch}); '
              f'max float ULP distance={max_ulp}; max scaled error={largest:.3g}; '
              f'over tolerance {args.tolerance:g}: {over}' + (f' (first: {first})' if first else ''))
        for line, (count, ulp) in sorted(by_line.items()):
            print(f'    {executable}.cpp:{line}: {count} samples, max ULP {ulp}: {source[line - 1].strip()[:90]}')
        if over:
            failures.append(executable)
    if args.bench and not args.sanitize:
        new, old = run_bench(build / 'current'), run_bench(build / 'baseline')
        print(f'{"benchmark (ns/op)":30s} {"baseline":>10s} {"current":>10s} {"delta":>8s}  checksum')
        for name, (value, checksum) in new.items():
            before, old_checksum = old[name]
            print(f'{name:30s} {before:10.3f} {value:10.3f} {100 * (value - before) / before:+7.1f}%  '
                  + ('same' if checksum == old_checksum else f'{old_checksum} -> {checksum}'))
    if failures:
        sys.exit('Legacy mismatch beyond tolerance in: ' + ', '.join(failures))
elif args.bench and not args.sanitize:
    for name, (value, checksum) in run_bench(build / 'current').items():
        print(f'{name:30s} {value:10.3f} ns/op  {checksum}')
