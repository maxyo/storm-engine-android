#!/usr/bin/env bash
# Run after ci_probe.sh has built pinned dependencies and completed regressions.
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
python3 "$repo/tools/android/verify_engine.py" --library "$work/engine/game/libengine.so" --ndk "$ndk"
python3 "$repo/tools/android/build_probe_apk.py" --android-jar "$work/tools/platform/android-35/android.jar" \
  --build-tools "$work/tools/buildtools/android-15" --sdl-source "$work/sources/SDL" --deps-prefix "$work/prefix" \
  --engine-library "$work/engine/game/libengine.so" --ndk "$ndk" --build-dir "$work/game-apk" \
  --engine-sources "$work/sources" --audio-sources "$work/audio-host/patched" --mimalloc-library "$work/engine/runtime/libmimalloc.so"
# The final artifact contains exactly the full-engine APK, not the earlier probe.
rm "$work/dist/storm-integration-probe-debug.apk"
cp "$work/game-apk/storm-engine-arm64-debug.apk" "$work/dist/"
cp "$repo/android/game/TESTING.md" "$work/dist/TESTING.md"
python3 - "$repo" "$work/dist" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
repo,dist=map(Path,sys.argv[1:]);apk=dist/'storm-engine-arm64-debug.apk';sha=hashlib.sha256(apk.read_bytes()).hexdigest()
(dist/'SHA256SUMS').write_text(sha+'  '+apk.name+'\n')
data={'kind':'game-port','game_port_ready':False,'commit':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),'abi':'arm64-v8a','min_api':24,'target_api':35,'ndk':'30.0.16248370','apk_sha256':sha,'verified':['full engine ARM64 linked','real SoundService open audio linked','host audio and voice policy synthetic checks','ASan/UBSan audio checks','Java and DEX compile','APK signature and alignment'],'not_verified':['device installation/startup','game resource corpus','Vulkan presentation/surface recovery','device audio and output route changes','touch controls in gameplay','save/load parity'],'readiness_reason':'Full engine bring-up artifact; device startup and game corpus validation outstanding.'}
(dist/'build-manifest.json').write_text(json.dumps(data,indent=2)+'\n')
PY
