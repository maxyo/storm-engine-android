#!/usr/bin/env python3
"""ParallelFor: every index exactly once, reuse, shutdown; under TSan and ASan."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-parallel-") as directory:
    for sanitizer in ("thread", "address,undefined"):
        binary = Path(directory) / ("check-" + sanitizer.split(",")[0])
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-pthread", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=" + sanitizer, "-fno-sanitize-recover=all",
            "-I" + str(root / "src/libs/util/include"),
            str(Path(__file__).with_name("check.cpp")), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=300)
