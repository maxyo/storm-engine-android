#!/usr/bin/env python3
"""Tavern mini-game export (mobile_table_game.hpp): script images, strings and facts as widgets."""
import json, pathlib, subprocess, tempfile
here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix='table-game-') as tmp:
    exe = pathlib.Path(tmp) / 'check'
    subprocess.run(['c++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'src/libs/xinterface/src'), str(here / 'check.cpp'), '-o', str(exe)], check=True)
    model = json.loads(subprocess.check_output([str(exe)], text=True))
    assert model['screen'] == 'boal_dice' and model['widgets'][-1]['node'] == 'ANDROID_TABLE_GAME'
print('PASS table game export: images with pictures, strings without beta lines, facts and missing facts; ASAN/UBSAN')
