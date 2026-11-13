#!/usr/bin/env python3
"""The scene sweep never deletes or writes through a symbolic link (tools/scene_sweep/run.py prepare):
a game folder with a linked RESOURCE is refused loudly and the linked folder stays intact; a read-only
game folder is swept from a writable copy without any change to the player's files."""
import importlib.util
import os
from pathlib import Path
import stat
import sys
import tempfile
import types

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('scene_sweep', root / 'tools/scene_sweep/run.py')
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)


def tree(path):
    return {str(p.relative_to(path)): (p.read_bytes() if p.is_file() else None) for p in sorted(path.rglob('*'))}


def make_game(game, resource=None):
    game.mkdir()
    (game / 'engine.ini').write_text('screen_x = 1\n')
    (game / 'options').write_text('options\n')
    (game / 'PROGRAM').mkdir()
    (game / 'SAVE').mkdir()
    (game / 'SAVE' / 'profile').write_text('save\n')
    if resource:
        (game / 'RESOURCE').symlink_to(resource, target_is_directory=True)
    else:
        (game / 'RESOURCE' / 'techniques').mkdir(parents=True)
        (game / 'RESOURCE' / 'techniques' / 'game.fx').write_text('game\n')


def shard(game, techniques, engine):
    return types.SimpleNamespace(arguments=types.SimpleNamespace(game=game, techniques=techniques),
                                 size=(640, 360), queue=['loc:test'], context={'engine': engine})


def expect_link_error(call, what):
    try:
        call()
    except sweep.LinkError as error:
        print(f'PASS refused: {what}: {error}'.splitlines()[0][:200])
        return
    raise AssertionError(f'not refused: {what}')


with tempfile.TemporaryDirectory(prefix='sweep-links-') as directory:
    base = Path(directory)
    techniques = base / 'src-techniques'
    techniques.mkdir()
    (techniques / 'engine.fx').write_text('engine\n')
    engine = base / 'engine'
    engine.write_text('binary\n')

    # inside(): a link anywhere on the way, or a path resolving outside, is refused.
    (base / 'scratch' / 'real').mkdir(parents=True)
    (base / 'scratch' / 'via').symlink_to(base / 'src-techniques', target_is_directory=True)
    assert sweep.inside(base / 'scratch', base / 'scratch' / 'real' / 'file') == base / 'scratch' / 'real' / 'file'
    expect_link_error(lambda: sweep.inside(base / 'scratch', base / 'scratch' / 'via' / 'engine.fx'), 'a linked parent')
    expect_link_error(lambda: sweep.inside(base / 'scratch', base / 'scratch' / 'via'), 'the link itself')
    expect_link_error(lambda: sweep.inside(base / 'scratch', base / 'elsewhere'), 'a path outside the copy')

    # A game folder whose RESOURCE links to a shared folder: refused, the shared folder untouched.
    shared = base / 'shared-RESOURCE'
    (shared / 'techniques').mkdir(parents=True)
    (shared / 'techniques' / 'game.fx').write_text('shared\n')
    before = tree(shared)
    linked = base / 'linked-game'
    make_game(linked, resource=shared)
    run = base / 'run-1'
    run.mkdir()
    expect_link_error(lambda: sweep.Shard.prepare(shard(linked, techniques, engine), run), 'a game folder with a linked RESOURCE')
    assert tree(shared) == before, 'the linked shared folder was changed'
    print('PASS the linked shared folder is unchanged')

    # --game given as a link to the real folder: the copy would be the link itself.
    (base / 'game-link').symlink_to(readonly_target := base / 'real-game', target_is_directory=True)
    make_game(readonly_target)
    run = base / 'run-3'
    run.mkdir()
    expect_link_error(lambda: sweep.Shard.prepare(shard(base / 'game-link', techniques, engine), run), '--game given as a link')

    # A read-only game folder (local-testing/extracted/app): prepared from a writable copy, the source unchanged.
    readonly = base / 'readonly-game'
    make_game(readonly)
    before = tree(readonly)
    for path in [readonly, *readonly.rglob('*')]:
        path.chmod(path.stat().st_mode & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))
    run = base / 'run-2'
    run.mkdir()
    game, control = sweep.Shard.prepare(shard(readonly, techniques, engine), run)
    assert (game / 'RESOURCE' / 'techniques' / 'engine.fx').read_text() == 'engine\n'
    assert not (game / 'RESOURCE' / 'techniques' / 'game.fx').exists()
    assert 'screen_x = 640' in (game / 'engine.ini').read_text()
    # Every file prepare() wrote is its own inode: unlinked first, never edited through a hard link
    # (the read-only source would refuse it with EACCES, a writable one would be changed).
    for written in ('engine.ini', 'options', 'PROGRAM/scene_sweep.c', 'RESOURCE/techniques/engine.fx'):
        assert os.stat(game / written).st_nlink == 1, f'{written} is still a hard link into another folder'
    assert tree(readonly) == before, 'the read-only game folder was changed'
    assert all(not os.stat(p).st_mode & stat.S_IWUSR for p in [readonly, *readonly.rglob('*')]), 'a source mode was changed'
    for path in [readonly, *readonly.rglob('*')]:
        path.chmod(path.stat().st_mode | stat.S_IWUSR)
    print('PASS a read-only game folder: swept from a writable copy, every written file its own inode, the source unchanged')
print('PASS scene sweep: no delete or write through a link, read-only game folders')
