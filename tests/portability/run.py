#!/usr/bin/env python3
"""x86 host oracle tests for native SSE and the portable scalar code paths."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-portability-") as directory:
    for scalar in (False, True):
        output = Path(directory) / ("scalar" if scalar else "sse")
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-frounding-math", "-ffp-contract=off",
                   "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                   "-I" + str(root / "src/libs/math/include"), "-I" + str(root / "src/libs/sea/src"),
                   str(root / "tests/portability/math.cpp"), "-o", str(output)]
        if scalar:
            command.append("-DSTORM_FORCE_SCALAR_MATH=1")
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)
