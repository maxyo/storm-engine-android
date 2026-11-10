#!/usr/bin/env python3
"""Host checks for deferred save deletion (src/libs/core/src/save_trash.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-save-trash-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/core/src"),
        str(Path(__file__).with_name("check.cpp")), "-lz", "-o", str(binary),
    ], check=True)
    work = Path(directory) / "work"
    work.mkdir()
    subprocess.run([str(binary), str(work)], check=True)
