#!/usr/bin/env python3
"""Check production slider parsing, capabilities and guarded setter dispatch."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = root / "src/libs/xinterface/src"
slider_source = (source / "nodes/xi_slide_line.cpp").read_text()
methods = slider_source.split("#ifdef __ANDROID__\n", 1)[1].split("#endif", 1)[0]
conversion = slider_source[slider_source.index("static int32_t SliderPositionFromNormalized"):].split("#ifdef __ANDROID__", 1)[0]
with tempfile.TemporaryDirectory(prefix="storm-mobile-settings-") as directory:
    build = Path(directory)
    (build / "slider_methods.inc").write_text(methods)
    (build / "slider_conversion.inc").write_text(conversion)
    binary = build / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I" + str(source), "-I" + str(build), str(here / "check.cpp"), "-o", str(binary)], check=True)
    data = json.loads(subprocess.check_output([str(binary)], text=True))
    assert data["schemaVersion"] == 2 and data["screenRevision"] == 7
    slider, radio = data["widgets"]
    assert (slider["min"], slider["max"], slider["step"]) == (0, 100, 1)
    assert slider["enabled"] is False and slider["unavailableReason"] == "android_color_correction"
    assert radio["exclusive"] is True and "min" not in radio
print("PASS slider range, parsing, stale revisions, disabled guards, setter dispatch, JSON and normalized save/reopen stability")
