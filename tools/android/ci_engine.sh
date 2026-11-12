#!/usr/bin/env bash
# Run after ci_dependencies.sh has built pinned dependencies and completed regressions.
# STORM_ANDROID_ABI=x86_64: emulator debug build into $work/engine-x86_64 (see abi.sh).
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
source "$repo/tools/android/abi.sh"
# Release (optimized, LTO, hidden visibility, debug info kept for symbol files) is
# what gets packaged; set STORM_NATIVE_BUILD_TYPE=Debug or RelWithDebInfo for diagnostics.
build_type=${STORM_NATIVE_BUILD_TYPE:-Release}
build_dir=${STORM_NATIVE_BUILD_DIR:-"$work/engine$sfx"}
case "$build_type" in
  Debug|Release|RelWithDebInfo|MinSizeRel) ;;
  *) echo "Unsupported STORM_NATIVE_BUILD_TYPE: $build_type" >&2; exit 2 ;;
esac
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then export JAVA_HOME="$JAVA_HOME_17_X64"; export PATH="$JAVA_HOME/bin:$PATH"; fi
ndk="$work/tools/ndk/android-ndk-r30"
python3 "$repo/tools/android/engine_sources.py" --directory "$work/sources"
source "$repo/tools/android/sources_lock.sh"
sources_lock shared "$work/sources" # held until this build exits: no checkout changes under it
# The host shaderc for the embedded shaders is built once per bgfx pin by the configure step
# (src/libs/renderer_bgfx/cmake/storm_shaderc.cmake); -DSTORM_SHADERC overrides it.
cmake -S "$repo" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$build_type" \
  -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/toolchains/android-$arch.cmake" -DSTORM_ANDROID_NDK="$ndk" \
  -DSTORM_ANDROID_DEPS="$work/prefix$sfx" -DSTORM_ANDROID_SOURCES="$work/sources" -DSTORM_AUDIO_SOURCES="$work/audio-host/patched" \
  ${STORM_SHADERC:+-DSTORM_SHADERC="$STORM_SHADERC"}
"$repo/tools/ccache/warmth" begin "$build_dir"
cmake --build "$build_dir" --target engine --parallel "${STORM_BUILD_JOBS:-2}"
"$repo/tools/ccache/warmth" end "$build_dir"
python3 "$repo/tools/android/verify_engine.py" --library "$build_dir/game/libengine.so" --ndk "$ndk" --abi "$abi" --allocator-library "$build_dir/runtime/libmimalloc.so"
