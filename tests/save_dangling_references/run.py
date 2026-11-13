#!/usr/bin/env python3
"""Save script globals whose ref/aref targets were freed, with the production writer (ASan).

--baseline <checkout> also builds the same scenario against a tree from before the fix
(e.g. a worktree at 900ecfa5) and requires AddressSanitizer to stop it with a
heap-use-after-free: the owner's crash in COMPILER::SaveVariable.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--glm-source', type=Path, required=True)
p.add_argument('--baseline', type=Path)
a = p.parse_args()
deps = a.dependency_sources.resolve()
test_dir = Path(__file__).resolve().parent

WRITER = ['void COMPILER::SaveData(', 'void COMPILER::SaveString(', 'void COMPILER::WriteVDword(',
          'void COMPILER::SaveVariable(', 'void COMPILER::SaveAttributesData(']
READER = ['bool COMPILER::ReadData(', 'uint32_t COMPILER::ReadVDword()', 'char *COMPILER::ReadString()',
          'bool COMPILER::ReadVariable(', 'void COMPILER::ReadAttributesData(']
BASELINE = ['bool COMPILER::FindReferencedVariable(', 'bool COMPILER::FindReferencedVariableByRootA(',
            'ATTRIBUTES *COMPILER::TraceARoot(']


def build(root, directory, baseline):
    source = (root / 'src/libs/core/src/compiler.cpp').read_text()
    parts = []
    # Complete, unchanged production methods of COMPILER; check.cpp declares that class around them.
    for start in WRITER + READER + (BASELINE if baseline else []):
        offset = source.index(start)
        parts.append(source[offset:source.index('\n}', offset) + 2])
    b = Path(directory)
    b.mkdir()
    (b / 'methods.inc').write_text('\n'.join(parts))
    # data.cpp does not use CoreImpl; its include would instantiate the full-engine singleton.
    (b / 'data.cpp').write_text((root / 'src/libs/core/src/data.cpp').read_text().replace(
        '#include "core_impl.h"', '#include "platform/platform.hpp"\n#include <cmath>\nusing std::isnan;'))
    (b / 'SDL2').symlink_to(deps / 'SDL/include', target_is_directory=True)
    includes = list((root / 'src/libs').glob('*/include')) + [root / 'src/libs/core/src', b,
        deps / 'fmt/include', deps / 'spdlog/include', deps / 'mimalloc/include',
        deps / 'fast_float/include', root / 'src/libs/renderer_bgfx/include', a.glm_source.resolve()]
    flags = ['-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-g',
             '-fno-omit-frame-pointer', '-fno-sanitize-recover=all'] + ['-I' + str(i) for i in includes]
    # The baseline's SaveData copies from a null buffer (harmless, fixed with the references):
    # only the address sanitizer there, so the run reaches the references.
    flags += ['-fsanitize=address', '-DSTORM_SAVE_BASELINE'] if baseline else ['-fsanitize=address,undefined']
    files = [test_dir / 'check.cpp', b / 'data.cpp'] + [root / 'src/libs/core/src' / name for name in
        ('attributes.cpp', 's_vartab.cpp')]
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *map(str, files), '-o', str(b / 'check')], check=True)
    return b / 'check'


with tempfile.TemporaryDirectory(prefix='storm-save-references-') as directory:
    subprocess.run([str(build(Path(__file__).resolve().parents[2], Path(directory) / 'fixed', False))], check=True)
    if a.baseline:
        result = subprocess.run([str(build(a.baseline.resolve(), Path(directory) / 'baseline', True))],
                                capture_output=True, text=True)
        assert result.returncode != 0 and 'heap-use-after-free' in result.stderr and 'TraceARoot' in result.stderr, \
            'the tree before the fix was expected to read freed memory in TraceARoot:\n' + result.stderr[-2000:]
        print('PASS: the baseline reads freed memory in TraceARoot (heap-use-after-free)')
