#!/usr/bin/env python3
"""An interface whose script has ended publishes no further model (xinterface mobile_interface_leaving.hpp)."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix="storm-interface-leaving-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(root / "src/libs/xinterface/src"),
                    str(here / "check.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
