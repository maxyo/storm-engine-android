#!/usr/bin/env python3
"""Build real SDL2/fmt native Android libraries (--abi, default arm64-v8a) from pinned local sources.

Does not download tools, accept licenses, build the engine, or package an APK.
"""
import argparse
from pathlib import Path
import subprocess

import android_abi

ROOT = Path(__file__).resolve().parents[2]
SDL_REVISION = "2e9821423a237a1206e3c09020778faacfe430be"
FMT_REVISION = "d141cdbeb0fb422a3fb7173b285fd38e0d1772dc"


# Third-party CMake projects compile through the Storm ccache launcher too (tools/ccache/README.md).
LAUNCHER = [f"-DCMAKE_{lang}_COMPILER_LAUNCHER={ROOT / 'tools/ccache/launcher'}" for lang in ("C", "CXX")]


def build_cached(directory, jobs):
    run([ROOT / "tools/ccache/warmth", "begin", directory])
    run(["cmake", "--build", directory, "--parallel", jobs])
    run([ROOT / "tools/ccache/warmth", "end", directory])


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def check_source(path, revision):
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise RuntimeError(f"{path}: expected {revision}, got {actual}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ndk", "sdl-source", "fmt-source", "build-dir", "prefix"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--abi", choices=sorted(android_abi.ABIS), default=android_abi.DEFAULT)
    args = parser.parse_args()
    ndk, sdl = args.ndk.resolve(), args.sdl_source.resolve()
    build, prefix = args.build_dir.resolve(), args.prefix.resolve()
    compiler = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    if not (compiler / f"{android_abi.triple(args.abi)}24-clang++").is_file():
        raise RuntimeError(f"Expected a Linux-host Android NDK with API 24 {args.abi} compiler")
    toolchain = android_abi.toolchain(ROOT, args.abi)
    fmt = args.fmt_source.resolve()
    check_source(fmt, FMT_REVISION)
    check_source(sdl, SDL_REVISION)
    for source, patch_name in ((sdl, "sdl2-android-ndk30.patch"), (fmt, "fmt8-clang21-constexpr.patch")):
        patch = ROOT / "tools/android/patches" / patch_name
        applied = subprocess.run(["git", "-C", str(source), "apply", "--reverse", "--check", str(patch)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not applied:
            run(["git", "-C", source, "diff", "--exit-code"])
            run(["git", "-C", source, "apply", "--check", patch])
            run(["git", "-C", source, "apply", patch])
    build.mkdir(parents=True, exist_ok=True)
    run(["cmake", "-S", sdl, "-B", build / "sdl", "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
         f"-DSTORM_ANDROID_NDK={ndk}", f"-DCMAKE_INSTALL_PREFIX={prefix}",
         "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF", *LAUNCHER,
         f"-DCMAKE_PROJECT_INCLUDE={ROOT / 'tools/android/sdl-ndk-cpufeatures.cmake'}"])
    build_cached(build / "sdl", args.jobs)
    run(["cmake", "--install", build / "sdl"])
    run(["cmake", "-S", fmt, "-B", build / "fmt", "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
         f"-DSTORM_ANDROID_NDK={ndk}", f"-DCMAKE_INSTALL_PREFIX={prefix}",
         "-DBUILD_SHARED_LIBS=ON", "-DFMT_TEST=OFF", "-DFMT_DOC=OFF", "-DCMAKE_CXX_STANDARD=20", *LAUNCHER])
    build_cached(build / "fmt", args.jobs)
    run(["cmake", "--install", build / "fmt"])
    for library in (prefix / "lib/libSDL2.so", prefix / "lib/libfmt.so"):
        android_abi.check_elf(library, args.abi)
        print(f"Verified {args.abi} ELF: {library}")
    print("Built dependency libraries only; full engine/APK and device validation remain outstanding.")

if __name__ == "__main__":
    main()
