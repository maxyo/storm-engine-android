#!/usr/bin/env python3
"""Fetch verified pinned sources and build standalone torrent JNI, or host tests.

All source/build/cache paths are under --work; no engine or APK build is performed.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    ("libtorrent", "libtorrent-1eb18faeae156d8dbbab42935c082f8b81f50989", "https://codeload.github.com/arvidn/libtorrent/tar.gz/1eb18faeae156d8dbbab42935c082f8b81f50989", "192fedf8a8d8bf88d097a7956612d0890e2f01a622ca3a00918ef0835c67a257"),
    ("boost", "boost_1_90_0", "https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2", "49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305"),
    ("try_signal", "try_signal-105cce59972f925a33aa6b1c3109e4cd3caf583d", "https://codeload.github.com/arvidn/try_signal/tar.gz/105cce59972f925a33aa6b1c3109e4cd3caf583d", "6f111b0d77429a8051be4faed06cf23fb03cf6ee233967c84fdd9d8d3b42ba8e"),
    ("openssl", "openssl-3.5.9", "https://www.openssl.org/source/openssl-3.5.9.tar.gz", "603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"),
)

def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)

def source(work, spec):
    name, directory, url, digest = spec
    archive = work / (name + (".tar.bz2" if name == "boost" else ".tar.gz"))
    if not archive.exists():
        partial = archive.with_suffix(".part")
        urllib.request.urlretrieve(url, partial)
        partial.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"Source checksum mismatch: {archive}")
    destination = work / directory
    if not (destination / ".storm-verified-source").is_file():
        with tarfile.open(archive) as tar:
            members = [m for m in tar.getmembers() if name != "boost" or "/boost/" in m.name or m.name.endswith("/LICENSE_1_0.txt")]
            tar.extractall(work, members=members, filter="data")
        (destination / ".storm-verified-source").write_text(digest + "\n")
    return destination

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, help="Omit to build and run host tests")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    work = args.work.resolve(); work.mkdir(parents=True, exist_ok=True)
    torrent, boost, signal, openssl = [source(work, spec) for spec in SOURCES]
    shutil.copytree(signal, torrent / "deps/try_signal", dirs_exist_ok=True)
    # Android stores PEM roots under hash-old filenames, incompatible with OpenSSL
    # 3 hashed-directory lookup. Load each PEM root without disabling validation.
    session_source = torrent / "src/session_impl.cpp"
    with tarfile.open(work / "libtorrent.tar.gz") as tar:
        original = tar.extractfile(f"{torrent.name}/src/session_impl.cpp").read().decode()
    needle = "m_ssl_ctx.set_default_verify_paths(ec);"
    if original:
        if original.count(needle) != 1:
            raise RuntimeError("Pinned libtorrent CA patch no longer applies")
        patch = '''m_ssl_ctx.set_default_verify_paths(ec);
#ifdef __ANDROID__
        // Storm Android platform trust roots
        for (auto root : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
            DIR* directory = opendir(root);
            if (!directory) continue;
            bool loaded = false;
            while (auto* file = readdir(directory)) {
                if (file->d_name[0] == '.') continue;
                m_ssl_ctx.load_verify_file(std::string(root) + "/" + file->d_name, ec);
                if (!ec) loaded = true;
            }
            closedir(directory);
            // An updated Conscrypt root set replaces the legacy system set.
            if (loaded) break;
        }
#endif'''
        patched = '#ifdef __ANDROID__\n#include <dirent.h>\n#endif\n' + original.replace(needle, patch)
        if session_source.read_text() != patched:
            session_source.write_text(patched)
    build = work / ("android" if args.ndk else "host")
    ssl_build = work / ("openssl-android" if args.ndk else "openssl-host")
    ssl_prefix = ssl_build / "prefix"
    ssl_build.mkdir(parents=True, exist_ok=True)
    if not (ssl_prefix / "lib/libssl.a").exists():
        env = os.environ.copy()
        target = "linux-x86_64"
        if args.ndk:
            target = "android-arm64"
            env["ANDROID_NDK_ROOT"] = str(args.ndk.resolve())
            env["PATH"] = str(args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin") + os.pathsep + env["PATH"]
        command = ["perl", openssl / "Configure", target, "no-shared", "no-tests", "no-apps", "no-docs", "no-module",
                   "-fPIC", f"--prefix={ssl_prefix}", "--libdir=lib"]
        if args.ndk:
            command += ["-D__ANDROID_API__=24"]
        run(command, cwd=ssl_build, env=env)
        run(["make", f"-j{args.jobs}"], cwd=ssl_build, env=env)
        run(["make", "install_sw"], cwd=ssl_build, env=env)
    command = ["cmake", "-S", ROOT / "android/torrent", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
               f"-DSTORM_LIBTORRENT_SOURCE={torrent}", f"-DBOOST_ROOT={boost}", f"-DBoost_INCLUDE_DIR={boost}", f"-DOPENSSL_ROOT_DIR={ssl_prefix}",
               f"-DOPENSSL_INCLUDE_DIR={ssl_prefix}/include", f"-DOPENSSL_SSL_LIBRARY={ssl_prefix}/lib/libssl.a",
               f"-DOPENSSL_CRYPTO_LIBRARY={ssl_prefix}/lib/libcrypto.a"]
    if args.ndk:
        command += [f"-DCMAKE_TOOLCHAIN_FILE={args.ndk.resolve()}/build/cmake/android.toolchain.cmake",
                    "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-24", "-DANDROID_STL=c++_shared"]
    cmake_env = os.environ.copy()
    cmake_env["PKG_CONFIG_LIBDIR"] = str(ssl_prefix / "lib/pkgconfig")
    cmake_env["PKG_CONFIG_PATH"] = str(ssl_prefix / "lib/pkgconfig")
    run(command, env=cmake_env)
    run(["cmake", "--build", build, "--parallel", args.jobs])
    if not args.ndk:
        run(["ctest", "--test-dir", build, "--output-on-failure"])
    else:
        run([args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip", "--strip-unneeded", build / "libstorm_torrent.so"])
    print(build / "libstorm_torrent.so")

if __name__ == "__main__":
    main()
