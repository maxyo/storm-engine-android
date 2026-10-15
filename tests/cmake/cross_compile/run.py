#!/usr/bin/env python3
"""Check host execution policy without downloading engine dependencies.

Generic cross mode deliberately uses the host compiler. This tests CMake's
execution policy only, and does not validate any Android binary.
"""
import pathlib
import subprocess
import tempfile

source = pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="storm-cmake-test-") as directory:
    root = pathlib.Path(directory)
    toolchain = root / "cross.cmake"
    toolchain.write_text('set(CMAKE_SYSTEM_NAME Generic)\n')
    for mode in ("native", "cross", "emulated"):
        build = root / mode
        args = ["cmake", "-S", str(source), "-B", str(build)]
        if mode != "native":
            args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        if mode == "emulated":
            # A harmless command prefix, exercising CMake's emulator insertion.
            args.append("-DCMAKE_CROSSCOMPILING_EMULATOR=cmake;-E;env")
        subprocess.run(args, check=True)
        subprocess.run(["cmake", "--build", str(build)], check=True)
        marker = build / "test-executed.txt"
        expected = mode != "cross"
        if marker.exists() != expected:
            raise RuntimeError(f"{mode}: unexpected test execution state")
        if expected and marker.read_text() != "42":
            raise RuntimeError(f"{mode}: incorrect linked fixture result")
        print(f"PASS {mode}: test executed={expected}", flush=True)
