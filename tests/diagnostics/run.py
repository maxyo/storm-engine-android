#!/usr/bin/env python3
"""Build the real logging and lifecycle sources and check when log lines reach the files.

Host only: no engine build or game data. Logs go to a temporary XDG_DATA_HOME.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dependency-sources', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
deps = args.dependency_sources.resolve()
diagnostics = root / 'src/libs/diagnostics'
with tempfile.TemporaryDirectory(prefix='storm-diagnostics-flush-') as temporary:
    build = Path(temporary)
    (build / 'SDL2').symlink_to(deps / 'SDL/include', target_is_directory=True)
    watermark = build / 'watermark.hpp'
    watermark.write_text('#pragma once\n#define STORM_BUILD_WATERMARK diagnostics_flush_check\n')
    binary = build / 'check'
    sources = [Path(__file__).with_name('flush_check.cpp'), diagnostics / 'src/logging.cpp',
               diagnostics / 'src/lifecycle_diagnostics_service.cpp',
               diagnostics / 'src/spdlog_sinks/syncable_sink.cpp', diagnostics / 'src/spdlog_sinks/file_helper.cpp']
    includes = [build, diagnostics / 'include', diagnostics / 'src', root / 'src/libs/core/include',
                root / 'src/libs/util/include', deps / 'spdlog/include', deps / 'fmt/include']
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-pthread', '-g', '-DSPDLOG_FMT_EXTERNAL',
                    '-DFMT_HEADER_ONLY', f'-DSTORM_WATERMARK_FILE="{watermark}"',
                    *['-I' + str(path) for path in includes], *map(str, sources),
                    '-l:libSDL2-2.0.so.0', '-o', str(binary)], check=True)
    environment = dict(os.environ, XDG_DATA_HOME=str(build / 'data'))
    subprocess.run([str(binary)], check=True, env=environment)
