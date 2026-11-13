#!/usr/bin/env python3
"""Host check that every engine logger's errors land in error.log (src/libs/diagnostics/src/logging.cpp)."""
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
with tempfile.TemporaryDirectory(prefix='storm-log-errors-') as directory:
    work = Path(directory)
    (work / 'SDL2').symlink_to(deps / 'SDL/include', target_is_directory=True)
    includes = [diagnostics / 'include', diagnostics / 'src', root / 'src/libs/util/include', work,
                deps / 'fmt/include', deps / 'spdlog/include']
    sources = [Path(__file__).with_name('check.cpp'), diagnostics / 'src/logging.cpp',
               diagnostics / 'src/spdlog_sinks/syncable_sink.cpp', diagnostics / 'src/spdlog_sinks/file_helper.cpp']
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *['-I' + str(path) for path in includes], *map(str, sources), '-o', str(work / 'check')], check=True)
    (work / 'stash').mkdir()
    subprocess.run([str(work / 'check'), str(work / 'stash')], check=True)
