#!/usr/bin/env python3
"""Exercise the production input parser and dialogue bridge with fake JNI/script services."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix="storm-dialogue-input-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-D__ANDROID__",
                    "-I" + str(here / "stubs"), "-I" + str(root / "src/libs/util/include"),
                    "-I" + str(root / "src/libs/dialog/src"), str(here / "check.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
