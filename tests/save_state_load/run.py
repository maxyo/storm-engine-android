#!/usr/bin/env python3
"""Save script globals the program cannot take back as a whole, load them with the production reader (ASan).

The writer methods come verbatim from compiler.cpp, the reader is save_state.hpp; DATA,
ATTRIBUTES and VarTable are the real sources.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--glm-source', type=Path, required=True)
a = p.parse_args()
deps = a.dependency_sources.resolve()
test_dir = Path(__file__).resolve().parent
root = test_dir.parents[1]

WRITER = ['void COMPILER::SaveData(', 'void COMPILER::SaveString(', 'void COMPILER::WriteVDword(',
          'void COMPILER::SaveVariable(', 'void COMPILER::SaveAttributesData(']

with tempfile.TemporaryDirectory(prefix='storm-save-state-') as directory:
    b = Path(directory)
    source = (root / 'src/libs/core/src/compiler.cpp').read_text()
    parts = []
    for start in WRITER:
        offset = source.index(start)
        parts.append(source[offset:source.index('\n}', offset) + 2])
    (b / 'methods.inc').write_text('\n'.join(parts))
    # data.cpp does not use CoreImpl; its include would instantiate the full-engine singleton.
    (b / 'data.cpp').write_text((root / 'src/libs/core/src/data.cpp').read_text().replace(
        '#include "core_impl.h"', '#include "platform/platform.hpp"\n#include <cmath>\nusing std::isnan;'))
    (b / 'SDL2').symlink_to(deps / 'SDL/include', target_is_directory=True)
    includes = list((root / 'src/libs').glob('*/include')) + [root / 'src/libs/core/src', b,
        deps / 'fmt/include', deps / 'spdlog/include', deps / 'mimalloc/include',
        deps / 'fast_float/include', root / 'src/libs/renderer_bgfx/include', a.glm_source.resolve()]
    flags = ['-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-g', '-fno-omit-frame-pointer',
             '-fno-sanitize-recover=all', '-fsanitize=address,undefined'] + ['-I' + str(i) for i in includes]
    files = [test_dir / 'check.cpp', b / 'data.cpp'] + [root / 'src/libs/core/src' / name for name in
        ('attributes.cpp', 's_vartab.cpp')]
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *map(str, files), '-o', str(b / 'check')], check=True)
    subprocess.run([str(b / 'check')], check=True)
