#!/usr/bin/env python3
"""The pinned fmt (tools/android/engine_sources.py) with a real compiler: formatting results, the
compile-time format checks, and float formatting under ASan+UBSan (fmt 8 copied overlapping ranges in
write_significand, a memcpy-param-overlap that stopped every ASan engine run at the first sea scene)."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fmt-source', type=Path, required=True)
args = parser.parse_args()
fmt = args.fmt_source.resolve()
compiler = [os.environ.get('CXX', 'c++'), '-std=c++20', '-I' + str(fmt / 'include')]
with tempfile.TemporaryDirectory(prefix='storm-fmt-') as directory:
    root = Path(directory)
    source = root / 'check.cpp'
    source.write_text(r'''#include <fmt/format.h>
#include <cstdio>
#include <cstdlib>
#include <random>
#define EXPECT(x) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", #x); std::abort(); } } while (0)
enum class Kind { a = 3 };
int main() {
  EXPECT(fmt::format(FMT_STRING("{:x}"), 255) == "ff");
  EXPECT(fmt::format(FMT_STRING("{:08x}"), 255) == "000000ff");
  EXPECT(fmt::format("{} {:.2f}", "value", 1.25) == "value 1.25");
  EXPECT(fmt::format("{}", fmt::underlying(Kind::a)) == "3");
  EXPECT(fmt::format("{} {} {:g}", 0.1f, 1e-7, 123456789.0f) == "0.1 1e-07 1.23457e+08");
  // Every float shape (shortest, fixed, exponent, padded) over many magnitudes.
  std::mt19937 random(1);
  std::uniform_real_distribution<double> value(-1e6, 1e6);
  size_t length = 0;
  for (int i = 0; i < 100000; ++i) {
    const double v = value(random) * (i % 7 == 0 ? 1e-9 : 1.0);
    const float f = static_cast<float>(v);
    length += fmt::format("{} {:.3f} {:g} {:e} {:10.4f} {}", v, v, f, f, f, static_cast<float>(i) / 3.0f).size();
  }
  EXPECT(length > 0);
}
''')
    binary = root / 'check'
    subprocess.run(compiler + ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                               '-fno-omit-frame-pointer', str(source), str(fmt / 'src/format.cc'), '-o', str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True, env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1:detect_leaks=0'))
    source.write_text('#include <fmt/format.h>\nauto bad = fmt::format(FMT_STRING("{:d}"), "text");\n')
    rejected = subprocess.run(compiler + ['-c', str(source), '-o', str(root / 'bad.o')],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if rejected.returncode == 0 or 'invalid format specifier' not in rejected.stderr:
        raise RuntimeError('Expected the invalid integer-format/string argument to be rejected at compile time:\n'
                           + rejected.stderr[-2000:])
print('PASS: fmt formats as expected, float formatting clean under ASan+UBSan, invalid type rejected at compile time')
