#!/usr/bin/env python3
"""Check actual stock selector shapes using engine ATTRIBUTES under sanitizers."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix="storm-mobile-inventory-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-DEX_OFF",
        "-Wno-deprecated-declarations",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-include", "memory",
        "-I" + str(root / "tests/mobile_inventory/stubs"),
        "-I" + str(root / "src/libs/core/include"),
        "-I" + str(root / "src/libs/util/include"),
        "-I" + str(root / "src/libs/xinterface/src"),
        str(here / "check.cpp"), str(root / "src/libs/core/src/attributes.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
