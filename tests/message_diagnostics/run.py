#!/usr/bin/env python3
"""Compile the real MESSAGE implementation; no engine build or device access."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources',type=Path,required=True)
p.add_argument('--glm-source',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2];deps=a.dependency_sources.resolve()
includes=[root/'src/libs/core/include',root/'src/libs/math/include',root/'src/libs/util/include',
          deps/'fmt/include',deps/'dxvk/include/native/directx',deps/'dxvk/include/native/windows',a.glm_source.resolve()]
with tempfile.TemporaryDirectory(prefix='storm-message-diagnostics-') as temporary:
    binary=Path(temporary)/'check'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-DFMT_HEADER_ONLY','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-fno-sanitize-recover=all',*['-I'+str(p) for p in includes],
                    str(root/'src/libs/core/src/message.cpp'),str(Path(__file__).with_name('check.cpp')),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
