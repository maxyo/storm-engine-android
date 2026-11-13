#!/usr/bin/env python3
"""Launcher of the host shaderc with a content-addressed result cache, shared by every build directory
and worktree (like ccache for C++): storm_shaders.cmake and storm_fx_shaders.cmake run

  python3 shaderc_cache.py <shaderc> <shaderc arguments...>

Each shader variant is compiled twice (OpenGL ES and SPIR-V); a fresh build directory, a rebase or an
edit and its revert used to compile all ~560 of them again. Here a result is found by
  key  = shaderc's content, the arguments (paths normalised, the output path left out), the content of
         the source and the varying definition;
  then the manifest of that key lists the files the compile included (shaderc --depends) with their
         content digests: a manifest entry whose files all still have those digests is a hit, and its
         stored output is copied to the output path.
A miss runs shaderc with --depends and stores output + manifest entry. Paths inside a git checkout are
stored relative to it, so another worktree's identical sources hit.

Cache: $STORM_SHADER_CACHE (default ~/.cache/storm-deps/shader-cache); entries not used for 30 days are
removed now and then. STORM_NO_SHADER_CACHE="<reason>" compiles without it, printed on every call.
Hits and misses are counted in shader-cache-stats.log of the build directory (--stats <file>).
"""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import time

VERSION = b'storm-shaderc-cache 2'
PATH_OPTIONS = {'-f', '-i', '--varyingdef'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with open(path, 'rb') as f:
        return digest(f.read())


def repo_root(path):
    for parent in [path, *path.parents]:
        if (parent / '.git').exists():
            return parent
    return None


def normalise(path, root, build):
    """'B:' path relative to the build directory, 'R:' relative to the checkout, or 'A:' absolute (real path)."""
    real = Path(os.path.realpath(path))
    if build and real.is_relative_to(build):
        return 'B:' + str(real.relative_to(build))
    if root and real.is_relative_to(root):
        return 'R:' + str(real.relative_to(root))
    return 'A:' + str(real)


def resolve(entry, root, build):
    kind, path = entry.split(':', 1)
    return {'B': build, 'R': root}.get(kind, Path('/')) / path


def shaderc_id(shaderc, cache):
    """Content digest of the shaderc binary, memoised by path, size and mtime."""
    stat = os.stat(shaderc)
    memo = cache / 'shaderc-ids' / digest(f'{os.path.realpath(shaderc)}|{stat.st_size}|{stat.st_mtime_ns}'.encode())
    try:
        return memo.read_text()
    except OSError:
        value = file_digest(shaderc)
        write_atomic(memo, value.encode())
        return value


def write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
        tmp.write(data)
    os.replace(tmp.name, path)


def count(stats, what):
    if stats:
        with open(stats, 'a') as log:
            log.write(what + '\n')


def prune(cache):
    limit = time.time() - 30 * 86400
    for path in (cache / 'objects').glob('*/*'):
        try:
            if path.stat().st_atime < limit:
                path.unlink()
        except OSError:
            pass


def main():
    args = sys.argv[1:]
    stats = None
    if args[:1] == ['--stats']:
        stats, args = args[1], args[2:]
    shaderc, args = args[0], args[1:]
    reason = os.environ.get('STORM_NO_SHADER_CACHE')
    if reason:
        print(f'shaderc_cache: not cached (STORM_NO_SHADER_CACHE: {reason})', file=sys.stderr)
        sys.exit(subprocess.run([shaderc, *args]).returncode)
    cache = Path(os.environ.get('STORM_SHADER_CACHE', Path.home() / '.cache/storm-deps/shader-cache'))
    output = Path(args[args.index('-o') + 1])
    source = Path(args[args.index('-f') + 1])
    root = repo_root(Path(os.path.realpath(source)).parent)
    # The build directory: the stats file's (CMAKE_BINARY_DIR); its patched bgfx view is the same in every one.
    build = Path(os.path.realpath(stats)).parent if stats else None

    key = hashlib.sha256(VERSION)
    key.update(shaderc_id(shaderc, cache).encode())
    i = 0
    while i < len(args):
        a = args[i]
        if a == '-o':
            i += 2
            continue
        key.update(b'\0' + a.encode())
        if a in PATH_OPTIONS and i + 1 < len(args):
            # Include directories count by what was included from them (the manifest's files).
            value = Path(args[i + 1])
            key.update(b'\0' + normalise(value, root, build).encode())
            if value.is_file():
                key.update(file_digest(value).encode())
            i += 2
            continue
        i += 1
    key = key.hexdigest()
    manifest_path = cache / 'manifests' / key[:2] / key
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, ValueError):
        manifest = []
    for entry in manifest:
        try:
            if all(file_digest(resolve(dep, root, build)) == d for dep, d in entry['deps'].items()):
                stored = cache / 'objects' / entry['result'][:2] / entry['result']
                data = stored.read_bytes()
                os.utime(stored)
                write_atomic(output, data)
                count(stats, 'hit')
                return
        except OSError:
            continue

    done = subprocess.run([shaderc, *args, '--depends'])
    depfile = Path(str(output) + '.d')
    if done.returncode != 0:
        depfile.unlink(missing_ok=True)
        sys.exit(done.returncode)
    count(stats, 'miss')
    try:
        text = depfile.read_text()
        depfile.unlink()
    except OSError:
        print(f'shaderc_cache: shaderc wrote no {depfile}: {output.name} is not cached', file=sys.stderr)
        return
    deps = {}
    for path in text.split(':', 1)[1].replace('\\\n', ' ').split():
        deps[normalise(path, root, build)] = file_digest(path)
    data = output.read_bytes()
    result = digest(key.encode() + json.dumps(deps, sort_keys=True).encode())
    write_atomic(cache / 'objects' / result[:2] / result, data)
    manifest = [e for e in manifest if e['deps'] != deps][-7:] + [{'deps': deps, 'result': result}]
    write_atomic(manifest_path, json.dumps(manifest).encode())
    if random.random() < 0.002:
        prune(cache)


if __name__ == '__main__':
    main()
