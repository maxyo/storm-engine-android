#!/usr/bin/env python3
"""Run actual save-reader methods with real DATA/ATTRIBUTES/EntityManager."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--glm-source', type=Path, required=True)
a = p.parse_args()
r = a.source_root.resolve()
deps = a.dependency_sources.resolve()
test_dir = Path(__file__).resolve().parent
source = (r/'src/libs/core/src/compiler.cpp').read_text()
# Extract complete, unchanged production methods. The fixture supplies only the
# compiler's surrounding variable table, codec, buffer and diagnostic plumbing;
# it does not contain a second implementation of the object deserializer.
methods = ['bool COMPILER::ReadData(', 'uint32_t COMPILER::ReadVDword()',
           'char *COMPILER::ReadString()', 'bool COMPILER::ReadVariable(',
           'void COMPILER::ReadAttributesData(']
parts = []
for start in methods:
    offset = source.index(start)
    parts.append(source[offset:source.index('\n}', offset) + 2].replace('COMPILER::', 'SaveReader::'))

with tempfile.TemporaryDirectory(prefix='storm-loaded-object-') as directory:
    b = Path(directory)
    (b/'reader.inc').write_text('\n'.join(parts))
    # data.cpp does not use CoreImpl. Its include otherwise instantiates the
    # inline full-engine singleton, which this bounded reader test cannot link.
    (b/'data.cpp').write_text((r/'src/libs/core/src/data.cpp').read_text().replace(
        '#include "core_impl.h"', '#include "platform/platform.hpp"\n#include <cmath>\nusing std::isnan;'))
    (b/'SDL2').symlink_to(deps/'SDL/include', target_is_directory=True)
    includes = list((r/'src/libs').glob('*/include')) + [r/'src/libs/core/src', b,
        deps/'fmt/include', deps/'spdlog/include', deps/'mimalloc/include',
        deps/'fast_float/include', deps/'dxvk/include/native/directx',
        deps/'dxvk/include/native/windows', a.glm_source.resolve()]
    flags = ['-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-fno-sanitize-recover=all'] + ['-I'+str(i) for i in includes]
    files = [test_dir/'check.cpp', b/'data.cpp'] + [r/'src/libs/core/src'/name for name in
        ('attributes.cpp', 's_vartab.cpp', 'entity_manager.cpp',
         'entity_container_cache.cpp', 'message.cpp')]
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *map(str, files),
                    '-o', str(b/'check')], check=True)
    subprocess.run([str(b/'check')], check=True)
