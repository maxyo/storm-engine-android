#!/usr/bin/env python3
"""Host checks for the compiled script cache files (src/libs/core/src/script_cache_files.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-script-cache-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/core/src"), "-I" + str(root / "src/libs/util/include"),
        str(Path(__file__).with_name("check.cpp")), "-o", str(binary),
    ], check=True)
    work = Path(directory) / "work"
    work.mkdir()
    subprocess.run([str(binary), str(work)], check=True)
