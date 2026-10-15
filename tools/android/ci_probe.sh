#!/usr/bin/env bash
# Reproducible-source integration probe pipeline; no game resources or FMOD.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${STORM_CI_WORK:-"$repo/build/android-ci"}
mkdir -p "$work/downloads" "$work/sources" "$work/tools" "$work/dist"
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
fetch_archive platform-35_r02.zip 0988cacad01b38a18a47bac14a0695f246bc76c1b06c0eeb8eb0dc825ab0c8e0 "$work/tools/platform"
fetch_archive build-tools_r35_linux.zip bd3a4966912eb8b30ed0d00b0cda6b6543b949d5ffe00bea54c04c81e1561d88 "$work/tools/buildtools"
checkout_source SDL https://github.com/libsdl-org/SDL.git 2e9821423a237a1206e3c09020778faacfe430be
checkout_source fmt https://github.com/fmtlib/fmt.git d141cdbeb0fb422a3fb7173b285fd38e0d1772dc
checkout_source dxvk https://github.com/Joshua-Ashton/dxvk-native.git a2dc99c407340432d4ba5bfa29efa685c27942ea
checkout_source glslang https://github.com/KhronosGroup/glslang.git 925503088e2bcd76921b1e102c37fc320bace254
python3 -m venv "$work/python"
"$work/python/bin/pip" install --disable-pip-version-check meson==1.9.1 ninja==1.13.0
export PATH="$work/python/bin:$PATH"
if [[ -n "${JAVA_HOME_17_X64:-}" ]]; then
    export JAVA_HOME="$JAVA_HOME_17_X64"
    export PATH="$JAVA_HOME/bin:$PATH"
fi
ndk="$work/tools/ndk/android-ndk-r30"
bt="$work/tools/buildtools/android-15"
cmake -S "$work/sources/glslang" -B "$work/glslang" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF -DBUILD_TESTING=OFF -DENABLE_GLSLANG_BINARIES=ON
cmake --build "$work/glslang" --target glslangValidator --parallel 2
python3 "$repo/tools/android/build_dependencies.py" --ndk "$ndk" --sdl-source "$work/sources/SDL" --fmt-source "$work/sources/fmt" --dxvk-source "$work/sources/dxvk" --glslang "$work/glslang/StandAlone/glslangValidator" --build-dir "$work/dependencies" --prefix "$work/prefix" --jobs 2
python3 "$repo/tests/cmake/cross_compile/run.py"
python3 "$repo/tests/portability/run.py"
python3 "$repo/tests/portability/check_toolchain.py"
python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL"
CXX="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++" python3 "$repo/tests/android_runtime/run.py" --sdl-source "$work/sources/SDL" --compile-only
python3 "$repo/tests/diagnostics/check_fmt.py" --fmt-source "$work/sources/fmt"
cmake -S "$repo/android/native" -B "$work/probe" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/toolchains/android-arm64.cmake" -DSTORM_ANDROID_NDK="$ndk" -DSTORM_ANDROID_DEPS="$work/prefix" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$work/probe" --parallel 2
python3 "$repo/tools/android/build_probe_apk.py" --android-jar "$work/tools/platform/android-35/android.jar" --build-tools "$bt" --sdl-source "$work/sources/SDL" --deps-prefix "$work/prefix" --probe-library "$work/probe/libstorm_probe.so" --ndk "$ndk" --build-dir "$work/apk"
cp "$work/apk/storm-integration-probe-debug.apk" "$work/dist/"
cp "$repo/android/README.md" "$work/dist/TESTING.md"
python3 - "$repo" "$work/dist" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
repo,dist=map(Path,sys.argv[1:])
apk=dist/'storm-integration-probe-debug.apk'
sha=hashlib.sha256(apk.read_bytes()).hexdigest()
(dist/'SHA256SUMS').write_text(sha+'  '+apk.name+'\n')
data={'kind':'integration-probe','game_port_ready':False,'commit':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),'abi':'arm64-v8a','min_api':24,'target_api':35,'ndk':'30.0.16248370','apk_sha256':sha,'verified':['ARM64 native dependency/probe linking','host math/lifecycle/storage checks','Java and DEX compile','APK signature and alignment'],'not_verified':['device installation or playback','Vulkan presentation','game engine linking or gameplay','FMOD or replacement audio']}
(dist/'build-manifest.json').write_text(json.dumps(data,indent=2)+'\n')
PY
