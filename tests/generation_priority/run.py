#!/usr/bin/env python3
"""Sanitized host checks for production generation priority; no renderer, device or model inference."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-generation-priority-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/renderer_bgfx/src/effects"),
        str(Path(__file__).with_name("check.cpp")), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
