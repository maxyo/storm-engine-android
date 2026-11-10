#!/usr/bin/env python3
"""Run every host-side engine check (no NDK, SDK, device or game data).

Fetches the pinned open-source sources the checks compile against (the same
revisions as tools/android/ci_dependencies.sh, tools/android/engine_sources.py
and cmake/glm.cmake), then runs each tests/*/run.py with the arguments it
declares (sources, glm, a build directory). Every failure is reported; the exit code is non-zero if
any check failed. CI runs it on pushes and pull requests to develop.

  python3 tools/ci/run_host_tests.py [--work build/host-ci] [--jobs N] [--only NAME ...]

The SDL input check needs a display: run under xvfb-run when headless.
"""
import argparse
import concurrent.futures
import io
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import time
import urllib.request

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, default=root / 'build/host-ci')
parser.add_argument('--only', nargs='*', help='run only these test directory names')
parser.add_argument('--jobs', type=int, default=os.cpu_count(), help='checks run in parallel')
args = parser.parse_args()
work = args.work.resolve()
sources = work / 'sources'


def checkout(name, url, revision):
    target = sources / name
    if not (target / '.git').exists():
        subprocess.run(['git', 'init', '-q', str(target)], check=True)
        subprocess.run(['git', '-C', str(target), 'remote', 'add', 'origin', url], check=True)
    head = subprocess.run(['git', '-C', str(target), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    if head != revision:
        subprocess.run(['git', '-C', str(target), 'fetch', '-q', '--depth', '1', 'origin', revision], check=True)
        subprocess.run(['git', '-C', str(target), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)


def prepare_sources():
    sources.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(root / 'tools/android/engine_sources.py'), '--directory', str(sources)],
                   check=True, stdout=subprocess.DEVNULL)
    pipeline = (root / 'tools/android/ci_dependencies.sh').read_text()
    for name in ('SDL', 'fmt'):
        match = re.search(rf'^checkout_source {name} (\S+) ([0-9a-f]{{40}})$', pipeline, re.M)
        if not match:
            sys.exit(f'{name} revision not found in tools/android/ci_dependencies.sh')
        checkout(name, match[1], match[2])
    glm = work / 'glm'
    url = re.search(r'URL (https://\S+)', (root / 'cmake/glm.cmake').read_text())[1]
    stamp = glm / '.storm-url'
    if not stamp.exists() or stamp.read_text() != url:
        with urllib.request.urlopen(url) as response:
            archive = tarfile.open(fileobj=io.BytesIO(response.read()))
        glm.mkdir(parents=True, exist_ok=True)
        archive.extractall(glm, filter='data')
        stamp.write_text(url)
    return next(path for path in glm.iterdir() if path.is_dir())


glm_source = prepare_sources()
os.environ.setdefault('STORM_GLM_SOURCE', str(glm_source))  # checks that locate GLM themselves
values = {
    '--sdl-source': sources / 'SDL',
    '--fmt-source': sources / 'fmt',
    '--dependency-sources': sources,
    '--glm-source': glm_source,
}
checks = sorted(root.glob('tests/*/run.py'))
if args.only:
    checks = [path for path in checks if path.parent.name in args.only]
# Options a check declares, with either quote style.
OPTION = r'''add_argument\(\s*['"](--[a-z-]+)['"]'''


def command_for(script):
    name = script.parent.name
    command = [sys.executable, str(script)]
    text = script.read_text()
    for option in re.findall(OPTION, text):
        if option == '--build-dir':
            command += [option, str(work / 'build' / name)]
        elif option in values:
            command += [option, str(values[option])]
    for option in re.findall(OPTION + r'[^)]*required\s*=\s*True', text):
        if option not in command:
            sys.exit(f'{script.relative_to(root)}: no host value for required option {option}')
    return command


# Checks that run the compiler themselves compile through the Storm ccache launcher
# (tools/ccache/cxx); CMake-based checks get it from cmake/StormBuildCache.cmake.
direct_environment = dict(os.environ, CXX=str(root / 'tools/ccache/cxx'),
                          STORM_CCACHE_CXX=os.environ.get('CXX', 'c++'))


def run(script):
    started = time.monotonic()
    environment = None if (script.parent / 'CMakeLists.txt').exists() else direct_environment
    result = subprocess.run(command_for(script), cwd=root, env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return script.parent.name, result, time.monotonic() - started


failed = []
with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
    for name, result, elapsed in pool.map(run, checks):
        print(f'{"PASS" if result.returncode == 0 else "FAIL"} {name} ({elapsed:.1f}s)', flush=True)
        if result.returncode:
            print(result.stdout[-20000:], flush=True)
            failed.append(name)
if failed:
    sys.exit(f'{len(failed)} of {len(checks)} host checks failed: ' + ', '.join(failed))
print(f'All {len(checks)} host checks passed')
