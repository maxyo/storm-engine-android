#!/usr/bin/env python3
"""What a change can affect: the host checks (tests/*/run.py), the game-data checks (tools/game_checks)
and the builds a gate runs (the UI host build, the Android engine build). Used by tools/ci/gate.py and
`run_host_tests.py --changed-since`; every check or build it leaves out is reported with the reason.

  affected.py [--base origin/develop] [--head HEAD] [--json]

Rules (README.md here):
- Markdown files (*.md) are documentation: they affect nothing.
- A change to the host CI itself (.github, tools/ci, tools/ccache, cmake, the top-level CMakeLists.txt,
  tests/<file>) selects everything.
- A host check is affected when its own directory changed, or when its files name a changed file, the
  changed file's directory, or the changed file's component (src/<kind>/<name>/, <top>/<name>/).
- A check that is a CMake project and has been built in --work is narrowed by its build graph: it stays
  affected only when a changed file is an input of that build (Ninja's dependency log and build.ninja),
  a new file lies in a directory the build reads, or its own directory changed.
- A build (UI host, Android engine) is affected the same way, from its build directory's graph; without
  a built directory it is affected ("no build graph yet").
- A game-data check is affected when its own directory changed or a changed file is one of the headers
  its check.cpp includes (the compiler's -MM list).
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
EVERYTHING = ('.github/', 'tools/ci/', 'tools/ccache/', 'cmake/')
TEXT_SUFFIXES = ('.py', '.txt', '.cpp', '.hpp', '.h', '.cmake', '.c', '.json', '.ini', '.sh')


def changed_files(base, head='HEAD', repo=root):
    """Files changed between the merge base of base and head, and head (plus uncommitted ones when head is HEAD)."""
    out = subprocess.check_output(['git', '-C', str(repo), 'diff', '--name-status', '--no-renames', f'{base}...{head}'],
                                  text=True).splitlines()
    if head == 'HEAD':
        out += subprocess.check_output(['git', '-C', str(repo), 'diff', '--name-status', '--no-renames', 'HEAD'],
                                       text=True).splitlines()
    files = {}
    for line in out:
        status, path = line.split('\t', 1)
        if path.endswith('.md'):
            continue  # documentation: read by no build or check
        files[path] = 'A' if status.startswith('A') and files.get(path, 'A') == 'A' else 'M'
    return files


def component(path):
    parts = path.split('/')
    if parts[0] == 'src' and len(parts) > 3:
        return '/'.join(parts[:3]) + '/'
    if len(parts) > 2:
        return '/'.join(parts[:2]) + '/'
    return None


class BuildGraph:
    """Every source path a Ninja build directory reads: the dependency log, the explicit inputs and the
    configure inputs of build.ninja. None when the directory has never been built."""

    def __init__(self, build_dir):
        self.build_dir = Path(build_dir)
        self.inputs = None
        ninja = shutil.which('ninja') or str(root.parent / 'storm-engine-android/build/android-ci/python/bin/ninja')
        if not (self.build_dir / '.ninja_deps').exists() or not (self.build_dir / 'build.ninja').exists():
            return
        paths = set()
        deps = subprocess.run([ninja, '-C', str(self.build_dir), '-t', 'deps'], capture_output=True, text=True)
        for line in deps.stdout.splitlines():
            if line.startswith('    '):
                paths.add(line.strip())
        inputs = subprocess.run([ninja, '-C', str(self.build_dir), '-t', 'inputs'], capture_output=True, text=True)
        if inputs.returncode != 0:  # Ninja older than 1.11: no 'inputs' tool, the dependency log alone is not enough
            return
        paths.update(line.strip() for line in inputs.stdout.splitlines())
        # The configure inputs (CMakeLists.txt, *.cmake, patches): the implicit inputs of CMake's re-run edge.
        for line in (self.build_dir / 'build.ninja').read_text(errors='ignore').splitlines():
            if line.startswith('build build.ninja') and ': RERUN_CMAKE' in line and ' | ' in line:
                paths.update(p.replace('$ ', ' ') for p in line.split(' | ', 1)[1].split(' '))
        resolved = set()
        for p in paths:
            path = Path(p) if os.path.isabs(p) else self.build_dir / p
            try:
                resolved.add(os.path.realpath(path))
            except OSError:
                pass
        self.inputs = resolved
        self.dirs = {os.path.dirname(p) for p in resolved}

    def hit(self, files, repo=root):
        """The first changed file the build reads, or None."""
        for path, status in files.items():
            full = os.path.realpath(repo / path)
            if full in self.inputs:
                return path
            if status == 'A' and os.path.dirname(full) in self.dirs:
                return f'{path} (new, in a directory the build reads)'
        return None


def everything_reason(files):
    for path in files:
        if path.startswith(EVERYTHING) or path == 'CMakeLists.txt' or (path.startswith('tests/') and path.count('/') == 1):
            return f'{path} changes the host CI itself'
    return None


def host_checks(files, work=None, repo=root):
    """{name: (affected, reason)} for every tests/<name>/run.py."""
    names = sorted(p.parent.name for p in (repo / 'tests').glob('*/run.py'))
    every = everything_reason(files)
    result = {}
    keys = {}
    for path in files:
        keys[path] = path
        keys[os.path.dirname(path) + '/'] = path
        if component(path):
            keys[component(path)] = path
    for name in names:
        own = next((p for p in files if p.startswith(f'tests/{name}/')), None)
        if every:
            result[name] = (True, every)
            continue
        if own:
            result[name] = (True, f'its own {own} changed')
            continue
        text = ''.join(f.read_text(errors='ignore') for f in (repo / 'tests' / name).rglob('*')
                       if f.is_file() and f.suffix in TEXT_SUFFIXES)
        key = next((k for k in keys if k in text), None)
        if not key:
            result[name] = (False, 'names no changed file, directory or component')
            continue
        reason = f'names {key} ({keys[key]})'
        if work and (repo / 'tests' / name / 'CMakeLists.txt').exists():
            graph = BuildGraph(Path(work) / 'build' / name)
            if graph.inputs is not None:
                hit = graph.hit(files, repo)
                result[name] = (True, f'its build reads {hit}') if hit else (
                    False, f'{reason}, but no changed file is an input of its build (ninja graph of build/{name})')
                continue
        result[name] = (True, reason)
    return result


def build_affected(build_dir, files, label, repo=root):
    every = everything_reason(files)
    if every:
        return True, every
    if any(p.startswith('tools/android/') for p in files):
        return True, 'tools/android changed'
    graph = BuildGraph(build_dir)
    if graph.inputs is None:
        return True, f'{label}: no build graph yet in {build_dir}'
    hit = graph.hit(files, repo)
    return (True, f'it reads {hit}') if hit else (False, f'no changed file is an input of {label} ({build_dir})')


def game_checks(files, repo=root):
    result = {}
    every = everything_reason(files)
    cxx = os.environ.get('STORM_CCACHE_CXX', 'c++')
    for check in sorted((repo / 'tools/game_checks').glob('*/run.py')):
        name = check.parent.name
        own = next((p for p in files if p.startswith(f'tools/game_checks/{name}/')), None)
        if every or own:
            result[name] = (True, every or f'its own {own} changed')
            continue
        # The -I directories its run.py passes, then the headers check.cpp really includes.
        includes = ['-I' + str(repo / m) for m in re.findall(r"'-I' \+ str\(root / '([^']+)'\)", check.read_text())]
        dep = subprocess.run([cxx, '-std=c++20', '-MM', *includes, str(check.parent / 'check.cpp')],
                             capture_output=True, text=True)
        if dep.returncode != 0:
            result[name] = (True, 'its include list could not be computed: ' + dep.stderr.strip()[:200])
            continue
        headers = {os.path.realpath(h) for h in dep.stdout.replace('\\\n', ' ').split()[1:]}
        hit = next((p for p in files if os.path.realpath(repo / p) in headers), None)
        result[name] = (True, f'check.cpp includes {hit}') if hit else (False, 'check.cpp includes no changed file')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', default='origin/develop')
    parser.add_argument('--head', default='HEAD')
    parser.add_argument('--work', type=Path, default=root / 'build/host-ci')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    files = changed_files(args.base, args.head)
    plan = {
        'changed': files,
        'host_checks': host_checks(files, args.work),
        'game_checks': game_checks(files),
        'ui_host': build_affected(root / 'build/ui-host', files, 'the UI host build'),
        'android': build_affected(root / 'build/android-engine', files, 'the Android engine build'),
    }
    if args.json:
        json.dump(plan, sys.stdout, indent=1)
        return
    print(f'{len(files)} changed files')
    for group in ('host_checks', 'game_checks'):
        for name, (run, why) in plan[group].items():
            print(f'{"RUN " if run else "skip"} {group[:-1]} {name}: {why}')
    for stage in ('ui_host', 'android'):
        run, why = plan[stage]
        print(f'{"RUN " if run else "skip"} {stage}: {why}')


if __name__ == '__main__':
    main()
