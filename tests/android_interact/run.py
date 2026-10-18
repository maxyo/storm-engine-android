#!/usr/bin/env python3
"""Check production interact binding and frame-latched ChrAction delivery."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "src/apps/engine/src/android_combat_state.hpp").read_text()
adapter = source[source.index("inline void updateCombatState("):source.index("inline jweak combatActivity")]
with tempfile.TemporaryDirectory(prefix="storm-interact-") as directory:
    build = Path(directory)
    (build / "adapter.inc").write_text(adapter)
    binary = build / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I" + str(root / "src/libs/util/include"), "-I" + str(build),
                    str(here / "check.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
