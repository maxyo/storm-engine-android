#!/usr/bin/env bash
# Run after ci_dependencies.sh has built pinned dependencies and completed regressions.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
# Release (optimized, LTO, hidden visibility, debug info kept for symbol files) is
# what gets packaged; set STORM_NATIVE_BUILD_TYPE=Debug or RelWithDebInfo for diagnostics.
build_type=${STORM_NATIVE_BUILD_TYPE:-Release}
build_dir=${STORM_NATIVE_BUILD_DIR:-"$work/engine"}
case "$build_type" in
  Debug|Release|RelWithDebInfo|MinSizeRel) ;;
  *) echo "Unsupported STORM_NATIVE_BUILD_TYPE: $build_type" >&2; exit 2 ;;
esac
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then export JAVA_HOME="$JAVA_HOME_17_X64"; export PATH="$JAVA_HOME/bin:$PATH"; fi
ndk="$work/tools/ndk/android-ndk-r30"
python3 "$repo/tools/android/engine_sources.py" --directory "$work/sources"
# Host shaderc from the engine's bgfx pin compiles the embedded shaders of the cross build.
cmake -S "$repo/src/libs/renderer_bgfx/host-tools" -B "$work/bgfx-host-tools" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/bgfx-host-tools" --parallel "${STORM_BUILD_JOBS:-2}"
cmake -S "$repo" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$build_type" \
  -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/toolchains/android-arm64.cmake" -DSTORM_ANDROID_NDK="$ndk" \
  -DSTORM_ANDROID_DEPS="$work/prefix" -DSTORM_ANDROID_SOURCES="$work/sources" -DSTORM_AUDIO_SOURCES="$work/audio-host/patched" \
  -DSTORM_SHADERC="$work/bgfx-host-tools/shaderc"
cmake --build "$build_dir" --target engine --parallel "${STORM_BUILD_JOBS:-2}"
python3 "$repo/tools/android/verify_engine.py" --library "$build_dir/game/libengine.so" --ndk "$ndk" --allocator-library "$build_dir/runtime/libmimalloc.so"
