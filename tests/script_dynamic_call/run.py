#!/usr/bin/env python3
"""Run the compiler's dynamic-call status store against real DATA/ATTRIBUTES; no engine build."""
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
source = (r/'src/libs/core/src/compiler.cpp').read_text()
# `call name()` must store its status through the method under test.
execute = source.index('bool COMPILER::BC_Execute(')
call = source[source.index('case CALL:', execute):source.index('case CALL_FUNCTION:', execute)]
assert 'StoreDynamicCallStatus(pVResult != nullptr);' in call, 'dynamic call no longer uses StoreDynamicCallStatus'
start = source.index('void COMPILER::StoreDynamicCallStatus(')
method = source[start:source.index('\n}', start) + 2].replace('COMPILER::', 'DynamicCall::')

with tempfile.TemporaryDirectory(prefix='storm-dynamic-call-') as directory:
    b = Path(directory)
    (b/'method.inc').write_text(method)
    # data.cpp does not use CoreImpl; its include would instantiate the full-engine singleton.
    (b/'data.cpp').write_text((r/'src/libs/core/src/data.cpp').read_text().replace(
        '#include "core_impl.h"', '#include "platform/platform.hpp"\n#include <cmath>\nusing std::isnan;'))
    (b/'SDL2').symlink_to(deps/'SDL/include', target_is_directory=True)
    includes = list((r/'src/libs').glob('*/include')) + [r/'src/libs/core/src', b,
        deps/'fmt/include', deps/'spdlog/include', deps/'mimalloc/include',
        deps/'fast_float/include', r/'third_party/d3d9-headers/directx',
        r/'third_party/d3d9-headers/windows', a.glm_source.resolve()]
    flags = ['-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-fno-sanitize-recover=all'] + ['-I'+str(i) for i in includes]
    files = [Path(__file__).with_name('check.cpp'), b/'data.cpp', r/'src/libs/core/src/attributes.cpp']
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *map(str, files), '-o', str(b/'check')], check=True)
    subprocess.run([str(b/'check')], check=True)
