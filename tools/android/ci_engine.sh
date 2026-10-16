#!/usr/bin/env bash
# Run after ci_dependencies.sh has built pinned dependencies and completed regressions.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then export JAVA_HOME="$JAVA_HOME_17_X64"; export PATH="$JAVA_HOME/bin:$PATH"; fi
ndk="$work/tools/ndk/android-ndk-r30"
python3 "$repo/tools/android/engine_sources.py" --directory "$work/sources"
cp "$work/dependencies/dxvk/src/d3d9/libdxvk_d3d9.so" "$work/prefix/lib/"
cmake -S "$repo" -B "$work/engine" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/toolchains/android-arm64.cmake" -DSTORM_ANDROID_NDK="$ndk" \
  -DSTORM_ANDROID_DEPS="$work/prefix" -DSTORM_ANDROID_SOURCES="$work/sources" -DSTORM_AUDIO_SOURCES="$work/audio-host/patched"
cmake --build "$work/engine" --target engine --parallel 2
python3 "$repo/tools/android/verify_engine.py" --library "$work/engine/game/libengine.so" --ndk "$ndk" --allocator-library "$work/engine/runtime/libmimalloc.so"
