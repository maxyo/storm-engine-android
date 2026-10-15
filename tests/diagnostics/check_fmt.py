#!/usr/bin/env python3
"""Verify patched fmt8 formatting and compile-time type checks with a real compiler."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fmt-source', type=Path, required=True)
parser.add_argument('--compile-only', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='storm-fmt-') as directory:
    root = Path(directory)
    source = root / 'check.cpp'
    source.write_text('''#include <fmt/format.h>
#include <cassert>
int main() {
  assert(fmt::format(FMT_STRING("{:x}"), 255) == "ff");
  assert(fmt::format(FMT_STRING("{:08x}"), 255) == "000000ff");
  assert(fmt::format("{} {:.2f}", "value", 1.25) == "value 1.25");
}
''')
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-DFMT_HEADER_ONLY',
               '-I' + str(args.fmt_source.resolve() / 'include')]
    output = root / ('check.o' if args.compile_only else 'check')
    subprocess.run(command + (['-c'] if args.compile_only else []) +
                   [str(source), '-o', str(output)], check=True)
    if not args.compile_only:
        subprocess.run([str(output)], check=True)
    source.write_text('#include <fmt/format.h>\nauto bad = fmt::format(FMT_STRING("{:d}"), "text");\n')
    rejected = subprocess.run(command + ['-c', str(source), '-o', str(root / 'bad.o')],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if rejected.returncode == 0 or 'invalid type specifier' not in rejected.stderr:
        raise RuntimeError('Expected invalid integer-format/string argument diagnostic')
    print('PASS: valid formatting ' + ('compiled' if args.compile_only else 'executed') +
          '; invalid type rejected at compile time')
