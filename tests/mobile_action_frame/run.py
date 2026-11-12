#!/usr/bin/env python3
"""Widget actions that queue script events run one per frame (mobile_action_frame.hpp), checked against the store's scripts."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory(prefix="storm-action-frame-") as directory:
    binary = Path(directory) / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
                    "-I" + str(root / "src/libs/xinterface/src"), str(here / "check.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
