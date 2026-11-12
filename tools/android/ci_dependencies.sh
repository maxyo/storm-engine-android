#!/usr/bin/env bash
# Native dependency and regression pipeline; no APK packaging or game resources.
# STORM_ANDROID_ABI=x86_64 builds the emulator prefix ($work/prefix-x86_64); see abi.sh.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
source "$repo/tools/android/abi.sh"
mkdir -p "$work/downloads" "$work/sources" "$work/tools"
fetch_archive() {
    local filename=$1 digest=$2 destination=$3
    if [[ ! -f "$work/downloads/$filename" ]]; then
        curl --fail --location --retry 3 "https://dl.google.com/android/repository/$filename" -o "$work/downloads/$filename"
    fi
    printf '%s  %s\n' "$digest" "$work/downloads/$filename" | sha256sum --check
    if [[ ! -d "$destination" ]]; then
        mkdir -p "$destination"
        unzip -q "$work/downloads/$filename" -d "$destination"
    fi
}
source "$repo/tools/android/sources_lock.sh"
checkout_source() {
    local name=$1 url=$2 revision=$3
    if [[ ! -d "$work/sources/$name/.git" ]]; then
        sources_lock exclusive "$work/sources" # one writer; builds reading the directory finish first
        if [[ ! -d "$work/sources/$name/.git" ]]; then
            git init -q "$work/sources/$name"
            git -C "$work/sources/$name" remote add origin "$url"
            git -C "$work/sources/$name" fetch --depth 1 origin "$revision"
            git -C "$work/sources/$name" checkout --detach FETCH_HEAD
        fi
        flock -u "$storm_sources_lock_fd"
    fi
    test "$(git -C "$work/sources/$name" rev-parse HEAD)" = "$revision"
}
fetch_archive android-ndk-r30-linux.zip 753611f410d002cfcd3f3dc2ef49aad532089d3180b436c060a90bf0fcb64df2 "$work/tools/ndk"
checkout_source SDL https://github.com/libsdl-org/SDL.git 2e9821423a237a1206e3c09020778faacfe430be
checkout_source fmt https://github.com/fmtlib/fmt.git d141cdbeb0fb422a3fb7173b285fd38e0d1772dc
sources_lock shared "$work/sources" # the dependency builds below compile from it
python3 -m venv "$work/python"
"$work/python/bin/pip" install --disable-pip-version-check cmake==3.22.1 ninja==1.13.0
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then
    export JAVA_HOME="$JAVA_HOME_17_X64"
    export PATH="$JAVA_HOME/bin:$PATH"
fi
ndk="$work/tools/ndk/android-ndk-r30"
python3 "$repo/tools/android/build_dependencies.py" --ndk "$ndk" --sdl-source "$work/sources/SDL" --fmt-source "$work/sources/fmt" --build-dir "$work/dependencies$sfx" --prefix "$work/prefix$sfx" --abi "$abi" --jobs "${STORM_BUILD_JOBS:-2}"
# Checks that run the compiler themselves compile through the Storm ccache launcher.
cxx="$repo/tools/ccache/cxx"
ndk_cxx="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/${triple}24-clang++"
python3 "$repo/tools/android/test_sdl_surface_lifecycle.py" --sdl-source "$work/sources/SDL"
# The audio experiment is independently tested; it is not part of the probe APK.
python3 "$repo/tests/audio/run.py" --build-dir "$work/audio-host" --cxx "$cxx"
python3 "$repo/tests/audio/run.py" --build-dir "$work/audio-sanitized" --sanitize --cxx "$cxx"
STORM_CCACHE_CXX="$ndk_cxx" python3 "$repo/tests/audio/run.py" --build-dir "$work/audio-android$sfx" --cxx "$cxx" --compile-only
python3 "$repo/tests/cmake/cross_compile/run.py"
python3 "$repo/tests/portability/run.py"
python3 "$repo/tests/portability/check_toolchain.py"
CXX="$cxx" python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL"
CXX="$cxx" STORM_CCACHE_CXX="$ndk_cxx" python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL" --compile-only
CXX="$cxx" python3 "$repo/tests/host_hooks/run.py"
CXX="$cxx" STORM_CCACHE_CXX="$ndk_cxx" python3 "$repo/tests/host_hooks/run.py" --sdl-source "$work/sources/SDL"
python3 "$repo/tests/diagnostics/check_fmt.py" --fmt-source "$work/sources/fmt"
