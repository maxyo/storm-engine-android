#!/usr/bin/env python3
"""Host checks for the script patch layer (platform/script_patches.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-script-patches-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/util/include"),
        str(Path(__file__).with_name("check.cpp")), "-o", str(binary),
    ], check=True)
    # The host tool (tools/script_patches) must keep building against the header.
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        "-I" + str(root / "src/libs/util/include"), str(root / "tools/script_patches/script_patch_tool.cpp"),
    ], check=True)
    work = Path(directory) / "work"
    work.mkdir()
    subprocess.run([str(binary), str(work)], check=True)
