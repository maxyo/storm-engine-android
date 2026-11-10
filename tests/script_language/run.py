#!/usr/bin/env python3
"""Script language and VM tests: compile and run tests/script_language/cases/*.c with script_check.

Each case runs every `int test_*()` function, which must return 1 without a runtime error; a case
whose first line is `// expect-error: <text>` must fail to compile with that error. Every case runs
twice: compiled, and loaded from a script cache another compiler wrote.

  python3 tests/script_language/run.py --script-check build/host/script_check
  python3 tests/script_language/run.py --build-dir build/host   (builds the script_check target)
"""
import argparse
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--script-check', type=Path, help='a built script_check binary')
p.add_argument('--build-dir', type=Path, help='a configured Linux host build directory (cmake/host.cmake)')
a = p.parse_args()
cases = sorted(Path(__file__).resolve().parent.joinpath('cases').glob('*.c'))
if a.script_check:
    tool = a.script_check.resolve()
elif a.build_dir and (a.build_dir / 'build.ninja').exists():
    subprocess.run(['cmake', '--build', str(a.build_dir), '--target', 'script_check'], check=True)
    tool = (a.build_dir / 'script_check').resolve()
else:
    sys.exit('script_language: pass --script-check or --build-dir with a configured host build '
             '(cmake -S . -B <dir> -G Ninja -DSTORM_HOST_SOURCES=... -DSTORM_AUDIO_SOURCES=..., see cmake/host.cmake)')
# once compiled, once loaded from the script cache (written by a first compiler)
plain = subprocess.run([str(tool), 'test', *map(str, cases)]).returncode
cached = subprocess.run([str(tool), 'test', '--cache', *map(str, cases)]).returncode
sys.exit(plain or cached)
