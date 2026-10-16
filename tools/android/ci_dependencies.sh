#!/usr/bin/env bash
# Native dependency and regression pipeline; no APK packaging or game resources.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
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
checkout_source() {
    local name=$1 url=$2 revision=$3
    if [[ ! -d "$work/sources/$name/.git" ]]; then
        git init -q "$work/sources/$name"
        git -C "$work/sources/$name" remote add origin "$url"
        git -C "$work/sources/$name" fetch --depth 1 origin "$revision"
        git -C "$work/sources/$name" checkout --detach FETCH_HEAD
    fi
    test "$(git -C "$work/sources/$name" rev-parse HEAD)" = "$revision"
}
fetch_archive android-ndk-r30-linux.zip 753611f410d002cfcd3f3dc2ef49aad532089d3180b436c060a90bf0fcb64df2 "$work/tools/ndk"
checkout_source SDL https://github.com/libsdl-org/SDL.git 2e9821423a237a1206e3c09020778faacfe430be
checkout_source fmt https://github.com/fmtlib/fmt.git d141cdbeb0fb422a3fb7173b285fd38e0d1772dc
checkout_source dxvk https://github.com/Joshua-Ashton/dxvk-native.git a2dc99c407340432d4ba5bfa29efa685c27942ea
checkout_source glslang https://github.com/KhronosGroup/glslang.git 925503088e2bcd76921b1e102c37fc320bace254
python3 -m venv "$work/python"
"$work/python/bin/pip" install --disable-pip-version-check cmake==3.22.1 meson==1.9.1 ninja==1.13.0
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then
    export JAVA_HOME="$JAVA_HOME_17_X64"
    export PATH="$JAVA_HOME/bin:$PATH"
fi
ndk="$work/tools/ndk/android-ndk-r30"
cmake -S "$work/sources/glslang" -B "$work/glslang" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF -DBUILD_TESTING=OFF -DENABLE_GLSLANG_BINARIES=ON
cmake --build "$work/glslang" --target glslangValidator --parallel 2
python3 "$repo/tools/android/build_dependencies.py" --ndk "$ndk" --sdl-source "$work/sources/SDL" --fmt-source "$work/sources/fmt" --dxvk-source "$work/sources/dxvk" --glslang "$work/glslang/StandAlone/glslangValidator" --build-dir "$work/dependencies" --prefix "$work/prefix" --jobs 2
# The audio experiment is independently tested; it is not part of the probe APK.
python3 "$repo/tests/audio/run.py" --build-dir "$work/audio-host"
python3 "$repo/tests/audio/run.py" --build-dir "$work/audio-sanitized" --sanitize
python3 "$repo/tests/audio/run.py" --build-dir "$work/audio-android" --cxx "$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++" --compile-only
python3 "$repo/tests/cmake/cross_compile/run.py"
python3 "$repo/tests/portability/run.py"
python3 "$repo/tests/portability/check_toolchain.py"
python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL"
CXX="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++" python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL" --compile-only
python3 "$repo/tests/diagnostics/check_fmt.py" --fmt-source "$work/sources/fmt"
