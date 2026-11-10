#!/usr/bin/env python3
"""List the natives a game's scripts declare that the engine does not provide (seconds, no build).

The engine provides a native either as an internal function (IntFuncTable in
src/libs/core/src/internal_functions.cpp, CL additions in src/libs/core/src/natives_ext_list.h) or
through a script library (`pFuncName = "..."` or `registerScriptFunction("...", ...)`).
Also lists natives our script libraries register that the game does not declare: registering an
undeclared native is a compile error ("native function '...' not declared").

    python3 tools/cl-host/natives_check.py --game <game dir> [--engine-root .]
Exit code 1 when a declared native is missing.
"""
import argparse
import re
import sys
import time
from pathlib import Path

DECL = re.compile(r'^\s*native\s+\w+\s+(\w+)\s*\(', re.M)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
LIB_REG = re.compile(r'pFuncName\s*=\s*"(\w+)"|registerScriptFunction\(\s*"(\w+)"')
EXT_REG = re.compile(r'^\s*STORM_NATIVE\(\s*\w+\s*,\s*"(\w+)"', re.M)


def program_dir(game: Path) -> Path:
    for p in game.iterdir():
        if p.is_dir() and p.name.lower() == 'program':
            return p
    sys.exit(f'no Program/ in {game}')


def declared(game: Path) -> dict:
    out = {}
    for f in sorted(program_dir(game).rglob('*')):
        if f.suffix.lower() not in ('.c', '.h'):
            continue
        text = COMMENT.sub('', f.read_text(encoding='latin-1'))
        for m in DECL.finditer(text):
            out.setdefault(m.group(1), f'{f.relative_to(game)}:{text.count(chr(10), 0, m.start()) + 1}')
    return out


def provided(root: Path):
    internal, library = set(), set()
    table = (root / 'src/libs/core/src/internal_functions.cpp').read_text()
    body = table[table.index('INTFUNCDESC IntFuncTable[] = {'):]
    body = COMMENT.sub('', body[:body.index('};')])
    internal.update(re.findall(r'"(\w+)"', body))
    ext = root / 'src/libs/core/src/natives_ext_list.h'
    if ext.exists():
        internal.update(EXT_REG.findall(ext.read_text()))
    for f in (root / 'src').rglob('*.cpp'):
        for m in LIB_REG.finditer(COMMENT.sub('', f.read_text(errors='replace'))):
            library.add(m.group(1) or m.group(2))
    return internal, library


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--engine-root', type=Path, default=Path(__file__).resolve().parents[2])
    a = ap.parse_args()
    t0 = time.monotonic()
    decl = declared(a.game)
    internal, library = provided(a.engine_root)
    # function names are case-insensitive in the script compiler
    have = {n.lower() for n in internal | library}
    missing = sorted((n for n in decl if n.lower() not in have), key=str.lower)
    lower = {n.lower() for n in decl}
    undeclared = sorted(n for n in library if n.lower() not in lower)
    print(f'declared natives: {len(decl)}; provided: {len(internal)} internal, {len(library)} library')
    print(f'missing ({len(missing)}):')
    for n in missing:
        print(f'  {n}\t{decl[n]}')
    print(f'library natives the game does not declare ({len(undeclared)}): {" ".join(undeclared)}')
    print(f'time: {time.monotonic() - t0:.2f}s')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
