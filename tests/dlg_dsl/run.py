#!/usr/bin/env python3
"""Check the Caribbean Legend template language (src/libs/core/src/dlg_dsl.cpp); no engine build.

  python3 tests/dlg_dsl/run.py                       unit checks under ASan/UBSan
  python3 tests/dlg_dsl/run.py --corpus <game dir>   also expand every value of every text file
                                                     of one language (optimised build, timing)
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('--corpus', type=Path, help='Caribbean Legend game directory (read-only)')
p.add_argument('--language', default='english', help='text directory under Resource/INI/texts')
a = p.parse_args()

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sources = [str(here / 'check.cpp'), str(root / 'src/libs/core/src/dlg_dsl.cpp')]
cxx = os.environ.get('CXX', 'c++')
common = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src/libs/core/src')]

with tempfile.TemporaryDirectory(prefix='storm-dlg-dsl-') as directory:
    b = Path(directory)
    builds = {'check': common + ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                                 '-fno-sanitize-recover=all']}
    if a.corpus:
        builds['corpus'] = common + ['-O2']
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(len(builds)) as pool:
        jobs = [pool.submit(subprocess.run, [cxx, *flags, *sources, '-o', str(b / name)], check=True)
                for name, flags in builds.items()]
        for job in jobs:
            job.result()
    print(f'compiled in {time.monotonic() - started:.1f}s', flush=True)
    subprocess.run([str(b / 'check')], check=True)

    if a.corpus:
        game = a.corpus.resolve()
        texts = next((d for d in (game / 'Resource/INI/texts').iterdir() if d.name.lower() == a.language.lower()), None)
        if texts is None:
            raise SystemExit(f'no text directory {a.language} under {game}/Resource/INI/texts')
        # Names of the game's script functions (names only, kept in the temporary directory),
        # so that `$name(` resolution behaves as in the game.
        signature = re.compile(r'^\s*(?:native\s+)?(?:string|int|float|bool|void|ref|aref|object)\s+'
                               r'([A-Za-z_]\w*)\s*\(', re.M)
        names = set()
        for script in (game / 'Program').rglob('*'):
            if script.suffix.lower() in ('.c', '.h'):
                names.update(signature.findall(script.read_bytes().decode('latin-1')))
        (b / 'functions.txt').write_text('\n'.join(sorted(names)) + '\n')
        started = time.monotonic()
        subprocess.run([str(b / 'corpus'), '--corpus', str(texts), '--functions', str(b / 'functions.txt')],
                       check=True)
        print(f'corpus run {time.monotonic() - started:.1f}s')
