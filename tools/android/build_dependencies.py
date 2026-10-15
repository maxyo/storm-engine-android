#!/usr/bin/env python3
"""Build real SDL2/fmt/DXVK native ARM64 libraries from pinned local sources.

Does not download tools, accept licenses, build the engine, or package an APK.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDL_REVISION = "2e9821423a237a1206e3c09020778faacfe430be"
FMT_REVISION = "d141cdbeb0fb422a3fb7173b285fd38e0d1772dc"
DXVK_REVISION = "a2dc99c407340432d4ba5bfa29efa685c27942ea"


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def check_source(path, revision):
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise RuntimeError(f"{path}: expected {revision}, got {actual}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ndk", "sdl-source", "fmt-source", "dxvk-source", "glslang", "build-dir", "prefix"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    ndk, sdl, dxvk = args.ndk.resolve(), args.sdl_source.resolve(), args.dxvk_source.resolve()
    build, prefix = args.build_dir.resolve(), args.prefix.resolve()
    compiler = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    if not (compiler / "aarch64-linux-android24-clang++").is_file():
        raise RuntimeError("Expected a Linux-host Android NDK with API 24 ARM64 compiler")
    fmt = args.fmt_source.resolve()
    check_source(fmt, FMT_REVISION)
    check_source(sdl, SDL_REVISION)
    check_source(dxvk, DXVK_REVISION)
    for source, patch_name in ((sdl, "sdl2-android-ndk30.patch"), (fmt, "fmt8-clang21-constexpr.patch"), (dxvk, "dxvk-native-arm64.patch")):
        patch = ROOT / "tools/android/patches" / patch_name
        applied = subprocess.run(["git", "-C", str(source), "apply", "--reverse", "--check", str(patch)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not applied:
            run(["git", "-C", source, "diff", "--exit-code"])
            run(["git", "-C", source, "apply", "--check", patch])
            run(["git", "-C", source, "apply", patch])
    build.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(args.glslang.resolve().parent) + os.pathsep + env["PATH"]
    if not args.glslang.is_file() or not shutil.which("meson", path=env["PATH"]):
        raise RuntimeError("Provide host glslangValidator and meson/ninja on PATH")
    run(["cmake", "-S", sdl, "-B", build / "sdl", "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake/toolchains/android-arm64.cmake'}",
         f"-DSTORM_ANDROID_NDK={ndk}", f"-DCMAKE_INSTALL_PREFIX={prefix}",
         "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF", "-DSDL_VULKAN=ON"], env=env)
    run(["cmake", "--build", build / "sdl", "--parallel", args.jobs], env=env)
    run(["cmake", "--install", build / "sdl"], env=env)
    run(["cmake", "-S", fmt, "-B", build / "fmt", "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake/toolchains/android-arm64.cmake'}",
         f"-DSTORM_ANDROID_NDK={ndk}", f"-DCMAKE_INSTALL_PREFIX={prefix}",
         "-DBUILD_SHARED_LIBS=ON", "-DFMT_TEST=OFF", "-DFMT_DOC=OFF", "-DCMAKE_CXX_STANDARD=20"], env=env)
    run(["cmake", "--build", build / "fmt", "--parallel", args.jobs], env=env)
    run(["cmake", "--install", build / "fmt"], env=env)
    cross = build / "android-arm64.ini"
    # Meson strings use single quotes. Refuse exotic paths rather than generating
    # an ambiguous cross file; subprocess commands themselves never use a shell.
    if any("'" in str(p) or "\n" in str(p) for p in (compiler, prefix)):
        raise RuntimeError("NDK/prefix paths cannot contain single quotes or newlines")
    cross.write_text(f"""[binaries]
c = '{compiler}/aarch64-linux-android24-clang'
cpp = '{compiler}/aarch64-linux-android24-clang++'
ar = '{compiler}/llvm-ar'
strip = '{compiler}/llvm-strip'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['-I{prefix}/include']
cpp_args = ['-I{prefix}/include']
c_link_args = ['-L{prefix}/lib']
cpp_link_args = ['-L{prefix}/lib']
""")
    command = ["meson", "setup", build / "dxvk", dxvk, "--cross-file", cross,
               "--buildtype=release", "-Ddxvk_native_force=true", "-Ddxvk_native_wsi=sdl2",
               "-Denable_d3d9=true", "-Denable_d3d10=false", "-Denable_d3d11=false", "-Denable_dxgi=false"]
    if (build / "dxvk/meson-private/coredata.dat").exists():
        command.append("--reconfigure")
    run(command, env=env)
    run(["meson", "compile", "-C", build / "dxvk", "-j", args.jobs], env=env)
    for library in (prefix / "lib/libSDL2.so", prefix / "lib/libfmt.so", build / "dxvk/src/d3d9/libdxvk_d3d9.so"):
        header = library.read_bytes()[:20]
        if header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[18:20], "little") != 183:
            raise RuntimeError(f"Not an ELF64 little-endian AArch64 library: {library}")
        print(f"Verified AArch64 ELF: {library}")
    print("Built dependency libraries only; full engine/APK and device validation remain outstanding.")

if __name__ == "__main__":
    main()
