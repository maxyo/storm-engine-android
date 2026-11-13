#!/usr/bin/env python3
"""Host check of the session logs: rotation and the running marker (src/libs/diagnostics/include/log_rotation.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-log-rotation-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/diagnostics/include"),
        str(Path(__file__).with_name("check.cpp")), "-o", str(binary),
    ], check=True)
    work = Path(directory) / "work"
    work.mkdir()
    subprocess.run([str(binary), str(work)], check=True)
