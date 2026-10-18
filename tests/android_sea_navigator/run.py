#!/usr/bin/env python3
import os,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='sea-navigator-') as tmp:
    binary=Path(tmp)/'check'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fsanitize=address,undefined','-I'+str(root/'src/libs/util/include'),str(Path(__file__).with_name('check.cpp')),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
