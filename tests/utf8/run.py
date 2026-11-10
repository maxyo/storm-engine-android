#!/usr/bin/env python3
"""Host checks for utf8.h decoding (surrogate rejection, bounded reads, linear cost)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-utf8-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-fsigned-char", "-Wall", "-Wextra",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/util/include"),
        str(Path(__file__).with_name("check.cpp")), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=120)
