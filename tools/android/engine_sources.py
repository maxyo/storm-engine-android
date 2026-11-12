#!/usr/bin/env python3
"""Fetch exact open-source engine dependency revisions; no proprietary SDKs.

The sources directory is shared by every worktree's builds (build/android-ci/sources): one
writer at a time. A checkout already at its pinned revision is only read (no git command that
writes, no lock). A missing or different one is fetched under an exclusive lock on
<directory>/.storm-sources.lock; builds that compile from the directory hold a shared lock on
the same file (ci_engine.sh, `sources_lock.sh`), so a checkout never changes under a running
build. A writer waits up to STORM_SOURCES_LOCK_TIMEOUT seconds (default 900) and then fails,
naming who holds the lock.
"""
import argparse
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import time

SOURCES = {
    'spdlog': ('https://github.com/gabime/spdlog.git', 'eb3220622e73a4889eee355ffa37972b3cac3df5'),
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
    """The writer's lock on the shared directory: waits for other writers and for running builds."""
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
                print(f'waiting for {directory} (held by {holder})', flush=True)
                said = True
            time.sleep(0.5)
    lock.seek(0)
    lock.truncate()
    lock.write(f'pid {os.getpid()} ({" ".join(sys.argv)}) in {os.getcwd()}\n')
    lock.flush()
    return lock


def ensure(directory, sources, timeout):
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    stale = {name: pin for name, pin in sources.items() if head(directory / name) != pin[1]}
    if stale:
        lock = exclusive(directory, timeout)
        try:
            for name, (url, revision) in stale.items():
                target = directory / name
                if head(target) == revision:
                    continue  # another writer did it while this one waited
                if not (target / '.git').exists():
                    subprocess.run(['git', 'init', '-q', str(target)], check=True)
                    subprocess.run(['git', '-C', str(target), 'remote', 'add', 'origin', url], check=True)
                subprocess.run(['git', '-C', str(target), 'fetch', '--depth', '1', 'origin', revision], check=True)
                subprocess.run(['git', '-C', str(target), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)
        finally:
            lock.seek(0)
            lock.truncate()
            lock.close()
    for name, (_, revision) in sources.items():
        found = head(directory / name)
        if found != revision:
            sys.exit(f'{directory / name} is at {found}, not {revision}')
        print(name, found, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    ensure(args.directory, SOURCES, float(os.environ.get('STORM_SOURCES_LOCK_TIMEOUT', '900')))
