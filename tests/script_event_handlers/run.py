#!/usr/bin/env python3
"""Run COMPILER::HasActiveEventHandler against the real script event table (s_eventtab.cpp)."""
from pathlib import Path
import os, subprocess, tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
core = root / "src/libs/core/src"
source = (core / "compiler.cpp").read_text()
start = source.index("bool COMPILER::HasActiveEventHandler(")
opening = source.index("{", start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
with tempfile.TemporaryDirectory(prefix="script-event-handlers-") as temp:
    temp = Path(temp)
    # s_eventtab.h needs only standard containers from the interpreter's data.h.
    (temp / "data.h").write_text("#pragma once\n#include <cstdint>\n#include <cstring>\n#include <vector>\n")
    (temp / "method.inc").write_text(source[start:end])
    for name in ("s_eventtab.h", "s_eventtab.cpp"): # copied: a quoted include would find the real data.h beside them
        (temp / name).write_text((core / name).read_text())
    binary = temp / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-fsanitize=address,undefined", "-I" + str(temp),
                    "-I" + str(root / "src/libs/util/include"), str(here / "check.cpp"),
                    str(temp / "s_eventtab.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
