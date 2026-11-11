#!/usr/bin/env python3
"""Check the sea AI's translation of the scripts' relation numbers; no engine build or game data."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--glm-source', type=Path, required=True)
a = p.parse_args()
r = Path(__file__).resolve().parents[2]
deps = a.dependency_sources.resolve()
sea_ai = r/'src/libs/sea_ai/src'


def require(path, text, why):
    assert text in (sea_ai/path).read_text(), f'{path}: {why}'


# Every relation crossing the script boundary goes through the scripts' numbering.
require('ai_helper.cpp', 'ScriptRelationNumbers().ToEngine(pData->GetInt())', 'GET_RELATION_EVENT results are not translated')
require('ai_ship.cpp', 'Helper.GetScriptRelationSafe(GetACharacter(), pObj->GetACharacter())', 'SeaAI.Update relations are not translated')
require('sea_ai.cpp', 'pVData->Set(Helper.GetScriptRelationSafe(pACharacter1, pACharacter2));', 'AI_MESSAGE_GET_RELATION is not translated')

source = (r/'src/libs/core/src/compiler.cpp').read_text()
start = source.index('std::optional<int32_t> COMPILER::FindIntegerDefine(')
method = source[start:source.index('\n}', start) + 2].replace('COMPILER::', '')

with tempfile.TemporaryDirectory(prefix='storm-sea-ai-relations-') as directory:
    b = Path(directory)
    (b/'method.inc').write_text(method)
    (b/'SDL2').symlink_to(deps/'SDL/include', target_is_directory=True)
    includes = list((r/'src/libs').glob('*/include')) + [r/'src/libs/core/src', sea_ai, b,
        deps/'fmt/include', deps/'spdlog/include', deps/'mimalloc/include',
        deps/'fast_float/include', r/'src/libs/renderer_bgfx/include', a.glm_source.resolve()]
    flags = ['-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-fno-sanitize-recover=all'] + ['-I'+str(i) for i in includes]
    files = [Path(__file__).with_name('check.cpp'), r/'src/libs/core/src/s_deftab.cpp']
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *map(str, files), '-o', str(b/'check')], check=True)
    subprocess.run([str(b/'check')], check=True)
