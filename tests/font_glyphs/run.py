#!/usr/bin/env python3
"""Check the bitmap font glyph tables (src/libs/renderer/src/font_glyphs.cpp): the Caribbean Legend
`char =` format with escapes, GroupTemplate inheritance and kerning, the TEHO fallback and the errors."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-font-glyphs-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src/libs/renderer/src'), '-isystem', str(root / 'src/libs/util/include'),
                    str(Path(__file__).with_name('check.cpp')), str(root / 'src/libs/renderer/src/font_glyphs.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
