#!/usr/bin/env python3
"""Exercise the production generator worker with deterministic inference and thermal inputs, no GPU/models."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
check=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="storm-relief-scheduler-") as directory:
    output=Path(directory)/"check"
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-pthread','-Wall','-Wextra','-Werror',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-I'+str(check/'stubs'),'-I'+str(root/'src/libs/renderer_bgfx/src'),
        '-I'+str(root/'src/libs/renderer_bgfx/include'),'-I'+str(root/'src/libs/renderer_bgfx/fx/include'),
        '-I'+str(root/'src/libs/relief'),str(check/'check.cpp'),
        os.environ.get('STORM_RELIEF_GENERATOR_SOURCE',str(root/'src/libs/renderer_bgfx/src/effects/relief_generator.cpp')),
        str(root/'src/libs/renderer_bgfx/fx/src/upscale.cpp'),'-o',str(output)],check=True)
    subprocess.run([str(output),directory],check=True,timeout=50)
