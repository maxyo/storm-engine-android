#!/usr/bin/env bash
# Build the portable UI runtime (src/libs/ui) for ARM64; its configure step writes the licence
# texts of its pinned dependencies. Needs the NDK prepared by ci_dependencies.sh.
# Output: $STORM_UI_BUILD_DIR/package/libstorm_ui.so and $STORM_UI_BUILD_DIR/licenses/.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
build_dir=${STORM_UI_BUILD_DIR:-"$work/ui"}
ndk=${STORM_ANDROID_NDK:-"$work/tools/ndk/android-ndk-r30"}
if [[ -d "$work/python/bin" ]]; then export PATH="$work/python/bin:$PATH"; fi
cmake -S "$repo/src/libs/ui" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/toolchains/android-arm64.cmake" -DSTORM_ANDROID_NDK="$ndk"
"$repo/tools/ccache/warmth" begin "$build_dir"
cmake --build "$build_dir" --target storm_ui_android --parallel "${STORM_BUILD_JOBS:-2}"
"$repo/tools/ccache/warmth" end "$build_dir"
echo "UI runtime: $build_dir/package/libstorm_ui.so"
