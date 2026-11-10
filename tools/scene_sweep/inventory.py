"""Scene inventory of a game directory, derived from its scripts.

Locations come from the init functions that PROGRAM/locations/locations_init.c
calls, islands from PROGRAM/islands/islands_init.c, so mods and new content are
picked up without editing this tool. scene_sweep.c reports the size of the
game's own tables after a new game and run.py fails the sweep when the two
disagree.
"""
from dataclasses import dataclass, field
import functools
import hashlib
from pathlib import Path
import re

LAND_KINDS = ('loc', 'night', 'combat')
# Kinds that are entered from land (see SceneSweep_StartSea in scene_sweep.c).
SEA_KINDS = ('sea', 'battle', 'boarding', 'map', 'storm')
BOOT_SCENES = ('menu:main', 'newgame:intro')


@dataclass
class Scene:
    id: str                      # "kind:name", the line passed to scene_sweep.c
    category: str
    fingerprint: str = ''        # changes when the scene's definition or model files change
    source: str = ''             # script file that defines it
    attributes: dict = field(default_factory=dict)

    @property
    def kind(self):
        return self.id.split(':', 1)[0]


def read_script(path):
    data = path.read_bytes()
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError:
        return data.decode('cp1251')


@functools.cache
def find_case_insensitive(root, relative):
    """Resolves a script path the way the engine does: backslashes, any letter case."""
    current = root
    for part in re.split(r'[\\/]+', relative.strip('\\/')):
        if not current.is_dir():
            return None
        match = next((entry for entry in current.iterdir() if entry.name.lower() == part.lower()), None)
        if match is None:
            return None
        current = match
    return current


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda match: '\n' * match.group().count('\n'), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def table_blocks(text, table=None):
    """Yields (id, block text) for each entry a script defines with `<object>.id = ...;`.

    The object is `table[n]` when `table` is given, otherwise anything (init
    functions also fill locations through a reference). The id is a string literal
    or a variable that was last assigned one (`sLocID = "..."; rLoc.id = sLocID;`).
    """
    target = rf'{table}\[\w+\]' if table else r'[A-Za-z_]\w*(?:\[\w+\])?'
    starts = []
    for match in re.finditer(rf'(?<![.\w]){target}\.id\s*=\s*(?:"([^"]*)"|([A-Za-z_]\w*))\s*;', text, flags=re.I):
        name = match.group(1)
        if name is None:
            assigned = re.findall(rf'\b{match.group(2)}\s*=\s*"([^"]*)"\s*;', text[:match.start()])
            if not assigned:
                raise SystemExit(f'cannot resolve the id assigned by `{match.group().strip()}`')
            name = assigned[-1]
        starts.append((match.start(), name))
    for index, (start, name) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(text)
        yield name, text[start:end]


def attribute(block, name):
    match = re.search(rf'\.{re.escape(name)}\s*=\s*"([^"]*)"\s*;', block, flags=re.I)
    return match.group(1) if match else ''


@functools.cache
def directory_fingerprint(path):
    if path is None or not path.is_dir():
        return 'missing'
    digest = hashlib.sha1()
    for entry in sorted(path.iterdir()):
        if entry.is_file():
            status = entry.stat()
            digest.update(f'{entry.name}:{status.st_size}:{status.st_mtime_ns};'.encode())
    return digest.hexdigest()


def fingerprint(game, block, models):
    digest = hashlib.sha1(block.encode())
    if models:
        digest.update(directory_fingerprint(find_case_insensitive(game, 'RESOURCE/MODELS/' + models)).encode())
    return digest.hexdigest()


def locations(game):
    program = game / 'PROGRAM'
    init = find_case_insensitive(program, 'locations/locations_init.c')
    if init is None:
        raise SystemExit(f'{program}/locations/locations_init.c not found: not a game directory')
    called = {name.lower() for name in re.findall(r'=\s*(LocationInit\w+)\s*\(', strip_comments(read_script(init)))}
    scenes = {}
    for path in sorted(init.parent.rglob('*.c')):
        text = strip_comments(read_script(path))
        for match in re.finditer(r'\bint\s+(LocationInit\w+)\s*\(', text):
            if match.group(1).lower() not in called:
                continue
            called.discard(match.group(1).lower())
            # The function runs to the next top-level definition or the end of the file.
            following = re.search(r'^\w[\w\s]*\([^;{]*\)\s*\{', text[match.end():], flags=re.M)
            body = text[match.start():match.end() + following.start() if following else len(text)]
            for name, block in table_blocks(body):
                if not name or name.upper() == 'NONE' or f'loc:{name}' in scenes:
                    continue
                models = attribute(block, 'filespath.models')
                kind = attribute(block, 'type').lower() or 'untyped'
                scenes[f'loc:{name}'] = Scene(f'loc:{name}', kind, fingerprint(game, block, models),
                                              str(path.relative_to(game)),
                                              {'island': attribute(block, 'islandId'), 'models': models})
    if called:
        raise SystemExit('location init functions called by locations_init.c but not found: ' + ', '.join(sorted(called)))
    return list(scenes.values())


def islands(game):
    path = find_case_insensitive(game / 'PROGRAM', 'islands/islands_init.c')
    if path is None:
        raise SystemExit(f'{game}/PROGRAM/islands/islands_init.c not found: not a game directory')
    result = []
    for name, block in table_blocks(strip_comments(read_script(path)), 'Islands'):
        if name and name not in result:
            models = attribute(block, 'filespath.models')
            result.append((name, fingerprint(game, block, models), str(path.relative_to(game))))
    return result


def tree_fingerprint(game, *relative):
    digest = hashlib.sha1()
    for item in relative:
        root = find_case_insensitive(game, item)
        for path in sorted(root.rglob('*')) if root else []:
            if path.is_file():
                status = path.stat()
                digest.update(f'{path.relative_to(game)}:{status.st_size}:{status.st_mtime_ns};'.encode())
    return digest.hexdigest()


def first_of_each_category(scenes):
    seen = {}
    for scene in scenes:
        seen.setdefault(scene.category, scene)
    return list(seen.values())


def build(game):
    """Every scene of the game: all locations, the sea around every island, and the
    special scenes (night, land combat, sea battle, boarding, world map, storm)."""
    land = locations(game)
    scenes = list(land)
    sea = islands(game)
    for name, mark, source in sea:
        scenes.append(Scene(f'sea:{name}', 'sea', mark, source))
    scripts = tree_fingerprint(game, 'PROGRAM')
    # One location of every type again at midnight (night models, lights, sky).
    for scene in first_of_each_category(land):
        name = scene.id.split(':', 1)[1]
        scenes.append(Scene(f'night:{name}', 'night', scene.fingerprint, scene.source))
    # Land combat where the game stages it: outdoors, in a town, underground, on a deck.
    for category in ('jungle', 'town', 'cave', 'deck_fight'):
        scene = next((item for item in land if item.category == category), None)
        if scene:
            scenes.append(Scene('combat:' + scene.id.split(':', 1)[1], 'combat', scene.fingerprint + scripts, scene.source))
    if sea:
        name, mark, source = sea[0]
        scenes.append(Scene(f'battle:{name}', 'sea battle', mark + scripts, source))
        scenes.append(Scene(f'boarding:{name}', 'boarding', mark + scripts, source))
    worldmap = tree_fingerprint(game, 'PROGRAM/worldmap', 'RESOURCE/MODELS/worldmap')
    scenes.append(Scene('map:world', 'world map', worldmap))
    scenes.append(Scene('storm:world', 'storm', worldmap + tree_fingerprint(game, 'PROGRAM/weather')))
    return scenes


def tables(game):
    """Sizes of the game's own tables, as scene_sweep.c reports them."""
    return {'locations': len(locations(game)), 'islands': len(islands(game))}
