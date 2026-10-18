#!/usr/bin/env python3
"""Check the semantic mailbox protocol, not gameplay/script outcome."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-gameplay-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    adapter = (root/'src/apps/engine/src/android_gameplay_state.hpp').read_text()
    method = adapter[adapter.index('inline GameplaySnapshot readGameplaySnapshot('):adapter.index('} // namespace storm::android')]
    (Path(d)/'snapshot.inc').write_text(method)
    snapshot = Path(d)/'snapshot'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g', '-DEX_OFF',
                    '-Wno-deprecated-declarations', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-include', 'memory', '-I'+str(root/'tests/mobile_inventory/stubs'),
                    '-I'+str(root/'src/libs/core/include'), '-I'+str(root/'src/libs/util/include'),
                    '-I'+d, str(Path(__file__).with_name('snapshot.cpp')),
                    str(root/'src/libs/core/src/attributes.cpp'), '-o', str(snapshot)], check=True)
    subprocess.run([str(snapshot)], check=True)
print('PASS: gameplay revision, scene reset, bounded action dispatch, stale rejection, tick rollover')
