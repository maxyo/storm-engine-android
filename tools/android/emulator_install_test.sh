#!/usr/bin/env bash
set -euo pipefail
mkdir -p build/install-test
out=$(realpath build/install-test)
base=https://github.com/maxyo/storm-engine-android/releases/download/android-experimental-9a1c91d808a5
curl -fL "$base/storm-engine-arm64-debug.apk" -o "$out/published.apk"
echo "9d7b36f3b8770918e5e4029d2b312e53ea2c113b243524454931951e6a226f3e  $out/published.apk" | sha256sum -c -
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"
sdkmanager 'platform-tools' 'emulator' 'system-images;android-35;google_apis;x86_64'
export ANDROID_AVD_HOME="$RUNNER_TEMP/storm-avd"
mkdir -p "$ANDROID_AVD_HOME"
echo no | avdmanager create avd --name storm-install --package 'system-images;android-35;google_apis;x86_64' --force
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"
accel=off
if [[ -r /dev/kvm && -w /dev/kvm ]]; then accel=on; fi
printf 'Host=%s acceleration=%s; x86_64 packaging/Java test only, ARM64 native unverified\n' "$(uname -m)" "$accel" | tee "$out/scope.txt"
emulator -avd storm-install -no-window -no-audio -no-boot-anim -no-snapshot -accel "$accel" -gpu swiftshader_indirect > "$out/emulator.log" 2>&1 &
emulator_pid=$!
trap 'rm -f "$out/java-test.keystore"; tail -80 "$out/emulator.log"; timeout 15 adb logcat -d > "$out/logcat.txt" 2>&1 || true; timeout 10 adb emu kill || true' EXIT
adb start-server
for i in $(seq 1 120); do
  if ! kill -0 "$emulator_pid" 2>/dev/null; then echo 'Emulator exited before adb'; exit 1; fi
  adb devices | grep -q 'emulator-.*device$' && break
  sleep 3
done
timeout 10 adb wait-for-device
for i in $(seq 1 180); do
  [[ $(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]] && break
  sleep 3
done
[[ $(adb shell getprop sys.boot_completed | tr -d '\r') == 1 ]]
adb shell input keyevent 82
adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.dalvik.vm.native.bridge
adb shell getprop > "$out/device-properties.txt"
adb install "$out/published.apk" > "$out/install.txt" 2>&1 || true
cat "$out/install.txt"
# Explicit ABI override tests package/Java only; never claim ARM64 execution.
adb install --abi arm64-v8a "$out/published.apk" > "$out/install-abi-override.txt" 2>&1 || true
cat "$out/install-abi-override.txt"
if ! grep -q 'Success' "$out/install.txt" && ! grep -q 'Success' "$out/install-abi-override.txt"; then
  # A separately signed Java-only derivative isolates launcher behavior. Never publish it.
  python3 - "$out" <<'PYAPK'
import sys,zipfile
from pathlib import Path
p=Path(sys.argv[1])
with zipfile.ZipFile(p/'published.apk') as src, zipfile.ZipFile(p/'java-only-unsigned.apk','w') as dst:
    for n in src.namelist():
        if not n.startswith(('lib/','META-INF/')): dst.writestr(n,src.read(n))
PYAPK
  bt=$(find "$ANDROID_HOME/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)
  keytool -genkeypair -keystore "$out/java-test.keystore" -storepass android -keypass android -alias androiddebugkey -dname CN=JavaOnlyTest -keyalg RSA -validity 2
  "$bt/zipalign" -f 4 "$out/java-only-unsigned.apk" "$out/java-only.apk"
  "$bt/apksigner" sign --ks "$out/java-test.keystore" --ks-pass pass:android "$out/java-only.apk"
  adb install "$out/java-only.apk" > "$out/java-only-install.txt" 2>&1
  cat "$out/java-only-install.txt"
  rm "$out/java-test.keystore" "$out/java-only-unsigned.apk" "$out/java-only.apk"
  echo 'Java-only derivative tested; original ARM64 install NOT verified' >> "$out/scope.txt"
fi
adb shell am start -W -n org.stormengine.port.game/org.stormengine.port.GameLauncherActivity > "$out/launch.txt" 2>&1 || true
cat "$out/launch.txt"
adb shell uiautomator dump /data/local/tmp/storm-ui.xml || true
adb pull /data/local/tmp/storm-ui.xml "$out/launcher.xml" || true
adb shell screencap -p /data/local/tmp/storm.png
adb pull /data/local/tmp/storm.png "$out/launcher.png"
# Exercise the Java launcher only if installation succeeded.
if adb shell pm path org.stormengine.port.game | grep -q '^package:'; then
  tap_text() {
    adb shell uiautomator dump /data/local/tmp/storm-ui.xml >/dev/null
    adb pull /data/local/tmp/storm-ui.xml "$out/current.xml" >/dev/null
    coords=$(python3 - "$out/current.xml" "$1" <<'PYUI'
import re,sys,xml.etree.ElementTree as E
for node in E.parse(sys.argv[1]).iter('node'):
    if node.get('text')==sys.argv[2]:
        x1,y1,x2,y2=map(int,re.findall(r'\d+',node.get('bounds')))
        print((x1+x2)//2,(y1+y2)//2);break
else: raise SystemExit('UI button missing: '+sys.argv[2])
PYUI
)
    adb shell input tap $coords
  }
  tap_text 'Start engine'
  adb shell uiautomator dump /data/local/tmp/storm-ui.xml
  adb pull /data/local/tmp/storm-ui.xml "$out/missing-assets.xml"
  grep -q 'engine.ini is missing' "$out/missing-assets.xml"
  tap_text 'Import game ZIP'
  adb shell uiautomator dump /data/local/tmp/storm-ui.xml
  adb pull /data/local/tmp/storm-ui.xml "$out/import-picker.xml"
  adb shell dumpsys activity activities > "$out/import-activity.txt"
  grep -q 'documentsui' "$out/import-activity.txt"
  echo 'PASS: launcher, missing assets, document import picker (Java UI only)' | tee "$out/ui-result.txt"
fi
rm "$out/published.apk"
