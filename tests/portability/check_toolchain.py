#!/usr/bin/env python3
"""Validate diagnostics using a dummy include, not a compiler or real NDK."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def check(name, foreign_abi):
    """Diagnostics of one toolchain file; foreign_abi is an ABI it must refuse."""
    wrapper = root / "cmake/toolchains" / name
    with tempfile.TemporaryDirectory(prefix="storm-toolchain-validation-") as directory:
        ndk = Path(directory)
        include = ndk / "build/cmake/android.toolchain.cmake"
        include.parent.mkdir(parents=True)
        include.write_text('message(STATUS "VALIDATION-ONLY: NDK include reached")\n')
        cases = [
            (["-DSTORM_ANDROID_NDK=/nonexistent/storm-ndk"], "Set STORM_ANDROID_NDK", False),
            ([f"-DSTORM_ANDROID_NDK={ndk}", f"-DANDROID_ABI={foreign_abi}"], "requires ANDROID_ABI", False),
            ([f"-DSTORM_ANDROID_NDK={ndk}", "-DANDROID_PLATFORM=android-21"], "requires API 24", False),
            ([f"-DSTORM_ANDROID_NDK={ndk}", "-DANDROID_PLATFORM=garbage"], "must have the form", False),
            ([f"-DSTORM_ANDROID_NDK={ndk}", "-DANDROID_STL=c++_static"], "uses c++_shared", False),
            ([f"-DSTORM_ANDROID_NDK={ndk}"], "NDK include reached", True),
        ]
        for args, expected, success in cases:
            result = subprocess.run(["cmake", *args, "-P", str(wrapper)], capture_output=True, text=True)
            if (result.returncode == 0) != success or expected not in result.stdout + result.stderr:
                raise RuntimeError(result.stdout + result.stderr)
            print("PASS:", name, expected)


check("android-arm64.cmake", "x86_64")
check("android-x86_64.cmake", "arm64-v8a")
