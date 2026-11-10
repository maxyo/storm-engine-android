#!/usr/bin/env python3
"""Alchemy export (mobile_alchemy.hpp): recipes, component facts, batches, craft and choose actions."""
import json, pathlib, subprocess, tempfile
here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix='alchemy-') as tmp:
    exe = pathlib.Path(tmp) / 'check'
    subprocess.run(['c++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'src/libs/xinterface/src'), str(here / 'check.cpp'), '-o', str(exe)], check=True)
    model = json.loads(subprocess.check_output([str(exe)], text=True))
    assert model['screen'] == 'alchemy' and model['widgets'][-1]['node'] == 'ANDROID_ALCHEMY'
print('PASS alchemy export: real recipes only, component use, batches and pot changes, guarded actions; ASAN/UBSAN')
