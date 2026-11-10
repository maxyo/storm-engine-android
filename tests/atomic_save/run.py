#!/usr/bin/env python3
"""Compile/run the save container and atomic replacement checks on the host.

Optional: --save <file> also round-trips a real legacy save and reports
compression timings for its script state (no game data is required otherwise).
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--save', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-atomic-save-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(root / "src/libs/core/src"),
        str(Path(__file__).with_name("check.cpp")), "-lz", "-o", str(binary),
    ], check=True)
    work = Path(directory) / "work"
    work.mkdir()
    subprocess.run([str(binary), str(work)] + ([str(a.save.resolve())] if a.save else []), check=True)
