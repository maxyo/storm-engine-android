#!/usr/bin/env python3
"""Exercise presentation policy and production GPU/font submission methods."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]


def method(path, signature):
    source = path.read_text()
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix="storm-interface-presentation-") as directory:
    build = Path(directory)
    renderer = root / "src/libs/renderer/src"
    (build / "renderer.inc").write_text(method(renderer / "s_device.cpp", "void DX9RENDER::DrawPrimitiveUP("))
    (build / "font.inc").write_text(method(renderer / "font.cpp", "int32_t FONT::Print("))
    binary = build / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
                    "-I" + str(root / "src/libs/util/include"), "-I" + str(build),
                    str(here / "check.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS immediate suppression, revision/mode guards, legacy compatibility, scoped GPU draw and font metrics")
