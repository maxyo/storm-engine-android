#!/usr/bin/env python3
"""Sail emblem export (mobile_sails_gerald.hpp): emblem rows, select action, price marker."""
import json, pathlib, subprocess, tempfile
here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix='sails-gerald-') as tmp:
    exe = pathlib.Path(tmp) / 'check'
    subprocess.run(['c++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'src/libs/xinterface/src'), str(here / 'check.cpp'), '-o', str(exe)], check=True)
    model = json.loads(subprocess.check_output([str(exe)], text=True))
    assert model['screen'] == 'sailsgerald' and model['widgets'][-1]['node'] == 'ANDROID_SAILS_GERALD'
print('PASS sail emblem export: rows with textures, guarded select, price and missing facts; ASAN/UBSAN')
