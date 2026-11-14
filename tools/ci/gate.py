#!/usr/bin/env python3
"""The push gate of engine develop: build and check what the change can affect, on the tree being pushed.

  tools/ci/gate.py [--rebase] [--push] [--full] [--tests NAME ...] [--base origin/develop]

Steps (README.md here):
1. fetch; with --rebase, rebase HEAD onto origin/develop;
2. plan from the diff base...HEAD (tools/ci/affected.py): the host checks, the game-data checks
   (tools/game_checks, on the local game), the UI host build + ctest and the Android arm64 engine
   build. Everything left out is printed with the reason (SKIP lines); --full runs everything
   (before a release) and --tests adds host checks by name;
3. the stages run in parallel, each logging to build/gate/<stage>.log; a summary with the times;
4. with --push: push HEAD to develop only if every stage passed and origin/develop has not moved
   since the plan (exit 3 when it moved: rebase and run again; the builds are incremental).

Build directories are the worktree's build/ui-host, build/host-ci and build/android-engine (the same
depth in every worktree, so ccache serves a fresh one). The Android SDK/NDK/dependencies come from
$STORM_CI_WORK (default <workspace>/storm-engine-android/build/android-ci), the engine sources of the
tree's pins from $STORM_CI_WORK/pins-<digest> (tools/android/engine_sources.py --shared); the game from $STORM_GAME_DIR
(default <workspace>/local-testing/extracted/app). Several worktrees landing at once: tools/ci/land.py.
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/ci'))
import affected  # noqa: E402

workspace = root.parent
ci_work = Path(os.environ.get('STORM_CI_WORK', workspace / 'storm-engine-android/build/android-ci'))
game_dir = Path(os.environ.get('STORM_GAME_DIR', workspace / 'local-testing/extracted/app'))
logs = root / 'build/gate'


def git(*args, check=True):
    return subprocess.run(['git', '-C', str(root), *args], check=check, capture_output=True, text=True).stdout.strip()


def run_logged(stage, commands, env=None):
    """Runs the commands one after another into build/gate/<stage>.log; the first failure stops."""
    log = logs / f'{stage}.log'
    with open(log, 'w') as out:
        for command in commands:
            out.write('$ ' + ' '.join(map(str, command)) + '\n')
            out.flush()
            done = subprocess.run([str(c) for c in command], cwd=root, env=env, stdout=out, stderr=subprocess.STDOUT)
            if done.returncode != 0:
                return False
    return True


def tail(stage, lines=15):
    text = (logs / f'{stage}.log').read_text(errors='replace').splitlines()
    return '\n'.join('    ' + line for line in text[-lines:])


def xvfb():
    return ['xvfb-run', '-a'] if shutil.which('xvfb-run') and not os.environ.get('DISPLAY') else []


def stage_ui_host():
    build = root / 'build/ui-host'
    commands = []
    if not (build / 'CMakeCache.txt').exists():
        commands.append(['cmake', '-S', root / 'src/libs/ui', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'])
    commands += [[root / 'tools/ccache/warmth', 'begin', build], ['cmake', '--build', build],
                 [root / 'tools/ccache/warmth', 'end', build],
                 [*xvfb(), 'ctest', '--test-dir', build, '-j8', '--output-on-failure']]
    return run_logged('ui_host', commands)


def stage_host_checks(names, jobs):
    return run_logged('host_checks', [[*xvfb(), sys.executable, root / 'tools/ci/run_host_tests.py', '--jobs', str(jobs),
                                       '--only', *names]])


def stage_game_checks(names):
    env = dict(os.environ, CXX=str(root / 'tools/ccache/cxx'), STORM_CCACHE_CXX=os.environ.get('CXX', 'c++'))
    return run_logged('game_checks', [[sys.executable, root / f'tools/game_checks/{name}/run.py', game_dir] for name in names],
                      env)


def stage_android():
    build = root / 'build/android-engine'
    env = dict(os.environ, PATH=f'{ci_work / "python/bin"}:{os.environ["PATH"]}')
    # The pinned sources of this tree (immutable shared checkouts, tools/android/engine_sources.py).
    prepared = subprocess.run([sys.executable, root / 'tools/android/engine_sources.py', '--shared', ci_work],
                              capture_output=True, text=True)
    if prepared.returncode:
        (logs / 'android.log').write_text(prepared.stdout + prepared.stderr)
        return False
    sources = prepared.stdout.strip()
    cache = build / 'CMakeCache.txt'
    configured = re.search(r'^STORM_ANDROID_SOURCES:\w+=(.*)$', cache.read_text(), re.M) if cache.exists() else None
    commands = []
    if not configured or configured[1] != sources:  # first build, or this tree's pins changed
        commands.append(['cmake', '-S', root, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                         f'-DCMAKE_TOOLCHAIN_FILE={root}/cmake/toolchains/android-arm64.cmake',
                         f'-DSTORM_ANDROID_NDK={ci_work}/tools/ndk/android-ndk-r30', f'-DSTORM_ANDROID_DEPS={ci_work}/prefix',
                         f'-DSTORM_ANDROID_SOURCES={sources}', f'-DSTORM_AUDIO_SOURCES={ci_work}/audio-host/patched'])
    commands += [[root / 'tools/ccache/warmth', 'begin', build], ['ninja', '-C', build, 'engine'],
                 [root / 'tools/ccache/warmth', 'end', build]]
    return run_logged('android', commands, env)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', default='origin/develop')
    parser.add_argument('--rebase', action='store_true', help='rebase HEAD onto --base first')
    parser.add_argument('--push', action='store_true', help='push HEAD to develop when everything passed')
    parser.add_argument('--full', action='store_true', help='every check and build, whatever changed (release)')
    parser.add_argument('--tests', nargs='*', default=[], help='host checks to run in addition')
    parser.add_argument('--jobs', type=int, default=6, help='host checks in parallel')
    parser.add_argument('--no-fetch', action='store_true')
    args = parser.parse_args()
    logs.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    if not args.no_fetch:
        git('fetch', '-q', 'origin')
    if args.rebase:
        done = subprocess.run(['git', '-C', str(root), 'rebase', args.base], capture_output=True, text=True)
        if done.returncode:
            subprocess.run(['git', '-C', str(root), 'rebase', '--abort'])
            sys.exit(f'GATE: rebase onto {args.base} conflicts:\n{done.stdout}{done.stderr}')
    dirty = git('status', '--porcelain', '--untracked-files=no')
    if dirty:
        if args.push:
            sys.exit('GATE: uncommitted changes would be built but not pushed; commit or stash them:\n' + dirty)
        print('GATE: WARNING, the gate includes uncommitted changes:\n' + dirty, flush=True)
    base_sha = git('rev-parse', args.base)
    files = affected.changed_files(args.base)
    if args.full:
        files = {'.github/full-gate': 'M'}  # selects everything, reason below
    work = root / 'build/host-ci'
    checks = affected.host_checks(files, work)
    games = affected.game_checks(files)
    ui = affected.build_affected(root / 'build/ui-host', files, 'the UI host build')
    android = affected.build_affected(root / 'build/android-engine', files, 'the Android engine build')
    reason = (lambda why: 'full gate (--full)') if args.full else (lambda why: why)
    names = sorted({n for n, (run, _) in checks.items() if run} | set(args.tests))
    plan_lines = [f'GATE {git("rev-parse", "--short", "HEAD")} over {args.base} {base_sha[:8]}: {len(files)} changed files']
    for name, (run, why) in checks.items():
        if not run and name not in args.tests:
            plan_lines.append(f'SKIP host check {name}: {why}')
    for name, (run, why) in games.items():
        plan_lines.append(f'{"RUN " if run else "SKIP"} game check {name}: {reason(why)}')
    plan_lines.append(f'{"RUN " if names else "SKIP"} host checks ({len(names)}): {" ".join(names) or "none affected"}')
    for label, (run, why) in (('ui_host', ui), ('android', android)):
        plan_lines.append(f'{"RUN " if run else "SKIP"} {label}: {reason(why)}')
    (logs / 'plan.txt').write_text('\n'.join(plan_lines) + '\n')
    skipped = sum(1 for line in plan_lines if line.startswith('SKIP host check'))
    for line in plan_lines:
        if not line.startswith('SKIP host check'):
            print(line, flush=True)
    if skipped:
        print(f'SKIP {skipped} host checks no changed file can affect (reasons: build/gate/plan.txt)', flush=True)

    stages = {}
    if ui[0]:
        stages['ui_host'] = stage_ui_host
    if names:
        stages['host_checks'] = lambda: stage_host_checks(names, args.jobs)
    game_names = [n for n, (run, _) in games.items() if run]
    if game_names:
        if not game_dir.is_dir():
            sys.exit(f'GATE: the game-data checks {game_names} need the game: {game_dir} does not exist (STORM_GAME_DIR)')
        stages['game_checks'] = lambda: stage_game_checks(game_names)
    if android[0]:
        if not (ci_work / 'tools/ndk').is_dir():
            sys.exit(f'GATE: the Android build needs the shared SDK/NDK in {ci_work} (STORM_CI_WORK)')
        stages['android'] = stage_android
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max(1, len(stages))) as pool:
        futures = {}
        for name, stage in stages.items():
            futures[pool.submit(lambda s=stage: (time.monotonic(), s(), time.monotonic()))] = name
        for future in concurrent.futures.as_completed(futures):
            name = futures[future]
            begin, ok, end = future.result()
            results[name] = (ok, end - begin)
            print(f'{"PASS" if ok else "FAIL"} {name} ({end - begin:.0f}s, build/gate/{name}.log)', flush=True)
            if not ok:
                print(tail(name), flush=True)
    failed = [name for name, (ok, _) in results.items() if not ok]
    total = time.monotonic() - started
    if failed:
        sys.exit(f'GATE FAILED ({total:.0f}s): {", ".join(failed)}')
    print(f'GATE PASSED on {git("rev-parse", "--short", "HEAD")} over {args.base} {base_sha[:8]} ({total:.0f}s)', flush=True)
    if not args.push:
        return
    git('fetch', '-q', 'origin')
    if git('rev-parse', args.base) != base_sha:
        print(f'GATE: {args.base} moved during the gate; rebase and run again (incremental)', flush=True)
        sys.exit(3)
    push = subprocess.run(['git', '-C', str(root), 'push', 'origin', 'HEAD:develop'], capture_output=True, text=True)
    print((push.stdout + push.stderr).strip().splitlines()[-1] if (push.stdout + push.stderr).strip() else '', flush=True)
    if push.returncode:
        sys.exit(f'GATE: push of {git("rev-parse", "HEAD")} failed')


if __name__ == '__main__':
    main()
