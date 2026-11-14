#!/usr/bin/env python3
"""Fetch exact open-source engine dependency revisions; no proprietary SDKs.

  engine_sources.py --directory DIR   checkouts in DIR/<name>, switched to the pins (one worktree's own
                                      directory, e.g. build/host-ci/sources)
  engine_sources.py --shared WORK     the machine's shared directory ($STORM_CI_WORK, every worktree's
                                      builds): prints the directory to build from, WORK/pins-<digest>
  engine_sources.py --check DIR       exit non-zero unless every DIR/<name> is at its pin (CMake runs it)

Shared: every revision is checked out once into WORK/source-store/<name>-<revision> and never changed
afterwards; WORK/pins-<digest of SOURCES> holds one symlink per name into the store. A worktree builds
from the directory of its own pins, so worktrees on different pins (a dependency bump landing while
others still build the old one) never switch a checkout under each other or rebuild after each other.
Store entries and pin directories are created whole (fetched beside, then renamed) under an exclusive
lock on WORK/source-store/.storm-sources.lock; readers take no lock. A writer waits up to
STORM_SOURCES_LOCK_TIMEOUT seconds (default 900) for another writer and then fails, naming it. Unused
store entries and pin directories can be deleted while no build reads them; they are fetched again.

--directory switches checkouts in place under the same kind of lock on DIR/.storm-sources.lock.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SOURCES = {
    'fmt': ('https://github.com/fmtlib/fmt.git', '407c905e45ad75fc29bf0f9bb7c5c2fd3475976f'),  # 12.1.0, the fmt spdlog 1.17.0 bundles
    'spdlog': ('https://github.com/gabime/spdlog.git', '79524ddd08a4ec981b7fea76afd08ee05f83755d'),  # 1.17.0
    'fast_float': ('https://github.com/fastfloat/fast_float.git', 'b7f9d6ca3980de0f47251921f307e88c894f6daa'),
    'mimalloc': ('https://github.com/microsoft/mimalloc.git', 'f412df7a2b64421e1f1d61fde6055a6ea288e8f5'),
    'catch2': ('https://github.com/catchorg/Catch2.git', 'c4e3767e265808590986d5db6ca1b5532a7f3d13'),
    # FFmpeg n8.1.3 (LGPL-2.1+ build only; see tools/ffmpeg/README.md).
    'ffmpeg': ('https://github.com/FFmpeg/FFmpeg.git', '1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7'),
}
LOCK_NAME = '.storm-sources.lock'


def head(target):
    if not (target / '.git').exists():
        return None
    result = subprocess.run(['git', '-C', str(target), 'rev-parse', 'HEAD'], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def exclusive(directory, timeout):
    """The writer's lock on a sources directory: waits for other writers (and, for --directory, builds)."""
    lock = open(directory / LOCK_NAME, 'a+')
    deadline = time.monotonic() + timeout
    said = False
    while True:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            lock.seek(0)
            holder = lock.read().strip() or 'a build compiling from it or a writer just starting'
            if time.monotonic() >= deadline:
                sys.exit(f'{directory} is locked by {holder} for more than {timeout:.0f} s; '
                         'a checkout there must not change under it. Retry when it is done '
                         '(STORM_SOURCES_LOCK_TIMEOUT sets the wait).')
            if not said:
                print(f'waiting for {directory} (held by {holder})', file=sys.stderr, flush=True)
                said = True
            time.sleep(0.5)
    lock.seek(0)
    lock.truncate()
    lock.write(f'pid {os.getpid()} ({" ".join(sys.argv)}) in {os.getcwd()}\n')
    lock.flush()
    return lock


def release(lock):
    lock.seek(0)
    lock.truncate()
    lock.close()


def fetch(target, url, revision):
    if not (target / '.git').exists():
        subprocess.run(['git', 'init', '-q', str(target)], check=True)
        subprocess.run(['git', '-C', str(target), 'remote', 'add', 'origin', url], check=True)
    subprocess.run(['git', '-C', str(target), 'fetch', '-q', '--depth', '1', 'origin', revision],
                   check=True, stdout=sys.stderr)
    subprocess.run(['git', '-C', str(target), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)


def stale(directory, sources):
    return {name: pin for name, pin in sources.items() if head(directory / name) != pin[1]}


def ensure(directory, sources, timeout):
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if stale(directory, sources):
        lock = exclusive(directory, timeout)
        try:
            for name, (url, revision) in stale(directory, sources).items():  # another writer may have done some
                fetch(directory / name, url, revision)
        finally:
            release(lock)
    check(directory, sources)


def pins_directory(work, sources):
    digest = hashlib.sha256(json.dumps(sorted(sources.items())).encode()).hexdigest()[:12]
    return work.resolve() / f'pins-{digest}'


def ensure_shared(work, sources, timeout):
    """Returns WORK/pins-<digest>: every name a symlink to its immutable store checkout."""
    store = work.resolve() / 'source-store'
    pins = pins_directory(work, sources)
    if pins.is_dir() and not stale(pins, sources):
        return pins
    store.mkdir(parents=True, exist_ok=True)
    lock = exclusive(store, timeout)
    try:
        for name, (url, revision) in sources.items():
            entry = store / f'{name}-{revision}'
            if head(entry) == revision:
                continue
            if entry.exists():
                sys.exit(f'{entry} is not a checkout of {revision}; delete it to fetch it again')
            partial = store / f'.{entry.name}.partial-{os.getpid()}'
            shutil.rmtree(partial, ignore_errors=True)
            fetch(partial, url, revision)
            partial.rename(entry)
        if not pins.is_dir():
            partial = work.resolve() / f'.{pins.name}.partial-{os.getpid()}'
            shutil.rmtree(partial, ignore_errors=True)
            partial.mkdir()
            for name, (_, revision) in sources.items():
                (partial / name).symlink_to(Path('..') / store.name / f'{name}-{revision}', target_is_directory=True)
            partial.rename(pins)
    finally:
        release(lock)
    check(pins, sources)
    return pins


def check(directory, sources):
    wrong = [f'{directory / name} is at {head(directory / name)}, not {revision}'
             for name, (_, revision) in sources.items() if head(directory / name) != revision]
    if wrong:
        sys.exit('\n'.join(wrong) + '\nPrepare the pinned sources with tools/android/engine_sources.py '
                 '(--shared $STORM_CI_WORK prints the directory to use, --directory DIR fills DIR).')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--directory', type=Path)
    mode.add_argument('--shared', type=Path, metavar='WORK')
    mode.add_argument('--check', type=Path, metavar='DIR')
    args = parser.parse_args()
    timeout = float(os.environ.get('STORM_SOURCES_LOCK_TIMEOUT', '900'))
    if args.directory:
        ensure(args.directory, SOURCES, timeout)
        for name, (_, revision) in SOURCES.items():
            print(name, revision, flush=True)
    elif args.shared:
        print(ensure_shared(args.shared, SOURCES, timeout), flush=True)
    else:
        check(args.check.resolve(), SOURCES)
