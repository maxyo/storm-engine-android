#!/usr/bin/env python3
"""Run production splash resource lifetime against two renderer generations."""
import argparse
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # keep the source tree clean
sys.path.insert(0, str(root / 'tests/glm_math'))
from glm_source import math_include_flags
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
args = parser.parse_args()
source = root / 'src/libs/ball_splash/src'

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

with tempfile.TemporaryDirectory(prefix='storm-splash-lifecycle-') as temporary:
    out = Path(temporary)
    for name in ('t_splash.h', 'ball_splash_defines.h'):
        (out / name).write_text((source / name).read_text())
    (out / 'sea_base.h').write_text('#pragma once\nstruct SEA_BASE {};\n')
    # t_splash.h takes its vector types from the real storm_glm.h (math_include_flags).
    for name in ('rands.h',):
        (out / name).write_text('#pragma once\n')
    (out / 'storm_render_types.h').write_text('''#pragma once
constexpr int D3DFVF_XYZ=2, D3DFVF_DIFFUSE=64, D3DFVF_TEX1=256, D3DFVF_TEX2=512;
constexpr int D3DFVF_TEXTUREFORMAT2=0, D3DUSAGE_WRITEONLY=8;
''')
    (out / 'dx9render.h').write_text((Path(__file__).with_name('renderer_fixture.hpp')).read_text())
    production = (source / 't_splash.cpp').read_text()
    globals_ = production[production.index('VDX9RENDER *TSplash::renderer'):production.index('uint32_t Desaturate')]
    methods = '\n'.join(function(production, signature) for signature in (
        'TSplash::TSplash()', 'TSplash::~TSplash()', 'void TSplash::Initialize(', 'void TSplash::Release()'))
    (out / 'production.inc').write_text(globals_ + '\n' + methods)
    binary = out / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(out), *math_include_flags(args.glm_source),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
