#!/usr/bin/env python3
"""The shared sources directory (tools/android/engine_sources.py, sources_lock.sh): a checkout at its
pin is only read, also while another process holds the writer's lock; concurrent writers of the same
pin both end at it with one of them waiting; a writer facing a running build (shared lock) waits and
then fails, naming the holder, instead of changing the checkout under it."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('engine_sources', root / 'tools/android/engine_sources.py')
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)


def git(*args, cwd=None):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


with tempfile.TemporaryDirectory(prefix='storm-sources-lock-') as scratch:
    scratch = Path(scratch)
    upstream = scratch / 'upstream'
    upstream.mkdir()
    git('init', '-q', cwd=upstream)
    git('config', 'uploadpack.allowReachableSHA1InWant', 'true', cwd=upstream)
    (upstream / 'a.txt').write_text('one\n')
    git('-c', 'user.name=t', '-c', 'user.email=t@t', 'add', '.', cwd=upstream)
    git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', 'one', cwd=upstream)
    first = git('rev-parse', 'HEAD', cwd=upstream)
    (upstream / 'a.txt').write_text('two\n')
    git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-am', 'two', cwd=upstream)
    second = git('rev-parse', 'HEAD', cwd=upstream)
    url = upstream.as_uri()
    shared = scratch / 'sources'

    # Two writers of the same pin at once: both end at it.
    errors = []
    def write():
        try:
            sources.ensure(shared, {'lib': (url, first)}, 30)
        except BaseException as error:  # SystemExit included
            errors.append(error)
    threads = [threading.Thread(target=write) for _ in range(2)]
    [t.start() for t in threads]
    [t.join() for t in threads]
    assert not errors and git('-C', str(shared / 'lib'), 'rev-parse', 'HEAD') == first, errors

    lock_script = f'source {root}/tools/android/sources_lock.sh; sources_lock shared {shared}; echo held; sleep 30'
    build = subprocess.Popen(['bash', '-c', lock_script], stdout=subprocess.PIPE, text=True)
    assert build.stdout.readline().strip() == 'held'
    try:
        # A checkout at its pin is only read: no wait for the running build.
        sources.ensure(shared, {'lib': (url, first)}, 0.5)
        # Another pin would change it under the build: the writer waits, then fails naming the holder.
        try:
            sources.ensure(shared, {'lib': (url, second)}, 1.5)
            sys.exit('a writer changed the checkout under a running build')
        except SystemExit as stop:
            assert 'locked by' in str(stop) and 'STORM_SOURCES_LOCK_TIMEOUT' in str(stop), stop
        assert git('-C', str(shared / 'lib'), 'rev-parse', 'HEAD') == first
    finally:
        build.kill()
        build.wait()
    # Once the build is gone the new pin is fetched.
    sources.ensure(shared, {'lib': (url, second)}, 5)
    assert git('-C', str(shared / 'lib'), 'rev-parse', 'HEAD') == second
print('shared sources: one writer, a pinned checkout read without a lock, no change under a running build')
