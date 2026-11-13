#!/usr/bin/env python3
"""User string files stay parsed after their last close (xinterface string_service/user_string_files.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix="storm-user-strings-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(root / "src/libs/util/include"),
                    "-I" + str(root / "src/libs/xinterface/src/string_service"),
                    str(here / "check.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
