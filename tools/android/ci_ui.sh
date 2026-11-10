#!/usr/bin/env bash
# Build the portable UI runtime (src/libs/ui) for ARM64 and copy the licence texts of
# its pinned dependencies. Needs the NDK prepared by ci_dependencies.sh.
# Output: $STORM_UI_BUILD_DIR/package/libstorm_ui.so and $STORM_UI_BUILD_DIR/licenses/.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
build_dir=${STORM_UI_BUILD_DIR:-"$work/ui"}
ndk=${STORM_ANDROID_NDK:-"$work/tools/ndk/android-ndk-r30"}
if [[ -d "$work/python/bin" ]]; then export PATH="$work/python/bin:$PATH"; fi
cmake -S "$repo/src/libs/ui" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/toolchains/android-arm64.cmake" -DSTORM_ANDROID_NDK="$ndk"
cmake --build "$build_dir" --target storm_ui_android --parallel "${STORM_BUILD_JOBS:-2}"
mkdir -p "$build_dir/licenses"
cp "$build_dir/_deps/storm_ui_rmlui-src/LICENSE.txt" "$build_dir/licenses/RmlUi-MIT.txt"
cp "$build_dir/_deps/storm_ui_freetype-src/docs/FTL.TXT" "$build_dir/licenses/FreeType-FTL.txt"
cp "$build_dir/_deps/storm_ui_lunasvg-src/LICENSE" "$build_dir/licenses/lunasvg-MIT.txt"
cp "$build_dir/_deps/storm_ui_lunasvg-src/plutovg/LICENSE" "$build_dir/licenses/plutovg-MIT.txt"
cp "$build_dir/_deps/storm_json-src/LICENSE.MIT" "$build_dir/licenses/nlohmann-json-MIT.txt"
echo "UI runtime: $build_dir/package/libstorm_ui.so"
