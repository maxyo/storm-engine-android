#!/usr/bin/env bash
set -euo pipefail
mkdir -p build/install-test
out=$(realpath build/install-test)
base=https://github.com/maxyo/storm-engine-android/releases/download/android-experimental-9a1c91d808a5
curl -fL "$base/storm-engine-arm64-debug.apk" -o "$out/published.apk"
echo "9d7b36f3b8770918e5e4029d2b312e53ea2c113b243524454931951e6a226f3e  $out/published.apk" | sha256sum -c -
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"
sdkmanager 'platform-tools' 'emulator' 'system-images;android-35;default;x86_64'
echo no | avdmanager create avd --name storm-install --package 'system-images;android-35;default;x86_64' --force
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"
accel=off
if [[ -r /dev/kvm && -w /dev/kvm ]]; then accel=on; fi
printf 'Host=%s acceleration=%s; x86_64 packaging/Java test only, ARM64 native unverified\n' "$(uname -m)" "$accel" | tee "$out/scope.txt"
emulator -avd storm-install -no-window -no-audio -no-boot-anim -no-snapshot -accel "$accel" -gpu swiftshader_indirect > "$out/emulator.log" 2>&1 &
trap 'adb logcat -d > "$out/logcat.txt" 2>&1 || true; adb emu kill || true' EXIT
adb start-server
timeout 600 adb wait-for-device
for i in $(seq 1 180); do
  [[ $(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]] && break
  sleep 3
done
[[ $(adb shell getprop sys.boot_completed | tr -d '\r') == 1 ]]
adb shell getprop > "$out/device-properties.txt"
adb install "$out/published.apk" > "$out/install.txt" 2>&1 || true
cat "$out/install.txt"
# Explicit ABI override tests package/Java only; never claim ARM64 execution.
adb install --abi arm64-v8a "$out/published.apk" > "$out/install-abi-override.txt" 2>&1 || true
cat "$out/install-abi-override.txt"
adb shell am start -W -n org.stormengine.port.game/org.stormengine.port.GameLauncherActivity > "$out/launch.txt" 2>&1 || true
cat "$out/launch.txt"
adb shell uiautomator dump /sdcard/storm-ui.xml || true
adb pull /sdcard/storm-ui.xml "$out/launcher.xml" || true
adb shell screencap -p /sdcard/storm.png
adb pull /sdcard/storm.png "$out/launcher.png"
rm "$out/published.apk"
