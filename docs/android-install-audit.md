# Published APK installation investigation — 2026-10-15

The reported Xiaomi/HyperOS `package is null` failure is **not yet reproduced or fixed**. No replacement APK was published during this investigation.

## Exact input

- Release: https://github.com/maxyo/storm-engine-android/releases/tag/android-experimental-9a1c91d808a5
- Source commit: `9a1c91d808a5b0c8761797b1d50764c1c212edb2`
- APK: `storm-engine-arm64-debug.apk`, 57,829,109 bytes
- SHA256: `9d7b36f3b8770918e5e4029d2b312e53ea2c113b243524454931951e6a226f3e`

The actual published APK was downloaded and its checksum matched. AAPT decoded package `org.stormengine.port.game`, minSDK 24, targetSDK 35, versionCode 1, ARM64-only native libraries and the exported launcher. ZIP integrity and APK v2/v3 signatures passed. All six native libraries have 16 KiB PT_LOAD alignment. These checks alone do not prove installation or gameplay.

## Real emulator evidence

Official Android Emulator 37.2.12 and SDK images were used. Local ARM64-on-x86_64 was rejected by the emulator (`QEMU2 emulator does not support arm64 CPU architecture`). Local tools also encountered a read-only home directory. No KVM/device permissions or security settings were changed.

On GitHub Actions, Android 15/API 35 AOSP x86_64 booted using software emulation. Installing the **original APK** returned:

```
INSTALL_FAILED_NO_MATCHING_ABIS: Failed to extract native libraries, res=-113
```

An explicit ARM64 ABI override was rejected as unsupported. This is an expected architecture mismatch on this test image, not evidence that it explains the Xiaomi error. It also does not establish that all later installation stages of the original ARM64 package pass.

For an isolated Java test, the native libraries were removed from a separately signed temporary derivative. That derivative installed successfully. Logcat records the launcher being displayed (`Displayed ... GameLauncherActivity ... +35s130ms`). It is **not** the released ARM64 APK and was not published as a release.

The missing-assets and import-picker assertions did **not** pass: UIAutomator captured an Android-owned `System UI isn't responding` dialog, and logcat contains ANRs in system phone/media services. The app launch timed out under software emulation. This is not evidence of a launcher crash or hidden buttons. No unverified launcher change was retained.

The Google APIs API 35 image did not finish booting within the bounded software-emulation wait. ARM translation and native runtime therefore remain unverified.

Evidence:

- Original install run: https://github.com/maxyo/storm-engine-android/actions/runs/38820035502
- Java-only install/UI run: https://github.com/maxyo/storm-engine-android/actions/runs/38822511907
- Diagnostic files: https://github.com/maxyo/storm-engine-android/actions/runs/38822511907/artifacts/12013684356
- Saved XML/logcat report: https://github.com/maxyo/storm-engine-android/actions/runs/38823653100

## Required next evidence / access

From the affected phone, collect the exact `adb install` result plus:

```
adb shell getprop ro.build.version.sdk
adb shell getprop ro.product.cpu.abilist
adb install storm-engine-arm64-debug.apk
```

Device name alone does not verify firmware ABI. No IMEI, serial number, credentials or game resources are needed. If relevant, collect PackageManager/logcat lines around that installation and verify the downloaded APK size/hash above.

The user's subsequent instruction prohibits running emulators in GitHub Actions. Use a compatible local/cloud test host or a real ARM64 device; GitHub is for builds and releases only. KVM access on an x86_64 runner would not resolve the ARM64 guest architecture rejection below. Do not label a new release installation-fixed until an identified failure is reproduced and the replacement passes the relevant installation test.

## Cloud-local continuation — 2026-10-15

Starting branch: `android/arm64-port`, commit `16ac589b5fc47b0ab8a28dc8d20ef7553518aee1`. No `AGENTS.md` or `.agents/skills` files were present in this checkout. Reported device: Xiaomi 15 Ultra, HyperOS `3.0.312.0.WOACNXM.C11`; exact Android version remains unknown and was not inferred from the device name.

Downloaded the unchanged release APK again: size, SHA256 and ZIP integrity match the exact input above. All six native libraries remain ARM64-only. No Java-only derivative was made. No commercial game resources were added.

Installed official stable Android Emulator **37.2.12.0**, build **16428233**, platform-tools **37.0.1**, and AOSP **API 35 ARM64 image revision 2** from Google's SDK repositories. Expected archive checksums and sizes from Google's repository metadata are recorded; the SDK installer removed its archive cache after installation, so no separate archive checksum recheck is claimed. The image requires emulator >=29.1.11, so this installed emulator meets its version requirement. See [Google SDK metadata](https://dl.google.com/android/repository/repository2-3.xml) and [system-image metadata](https://dl.google.com/android/repository/sys-img/android/sys-img2-3.xml).

All SDK, HOME, Android user, AVD and Java preference paths were placed under `/workspace/android-diagnostic`. The initial SDK installation emitted a Java preference warning; setting `JAVA_TOOL_OPTIONS=-Duser.home=/workspace/android-diagnostic/home` and creating its preference directory resolved that issue for AVD creation. Command-line tools were placed in the standard SDK `cmdline-tools/latest` layout before the final test. No system permissions or security settings were changed.

AVD creation succeeded. The official launcher found the actual ARM64 system-image path, then immediately rejected the host/guest pair, even with software emulation requested:

```text
FATAL | Avd's CPU Architecture 'arm64' is not supported by the QEMU2 emulator on x86_64 host. System image must match the host architecture.
exit_code=1
```

The host is x86_64, `/dev/kvm` is absent, and `emulator -accel-check` also reports `KVM requires a CPU that supports vmx or svm`. The architecture rejection is the direct blocker; it occurs before Android boots, independently of a writable HOME. [Android's acceleration documentation](https://developer.android.com/studio/run/emulator-acceleration) also distinguishes matching host/image architectures for acceleration.

Final launch command (45-second upper bound; process actually exited immediately):

```sh
env HOME=/workspace/android-diagnostic/home \
  ANDROID_USER_HOME=/workspace/android-diagnostic/home/.android \
  ANDROID_AVD_HOME=/workspace/android-diagnostic/avd \
  ANDROID_HOME=/workspace/android-diagnostic/sdk \
  ANDROID_SDK_ROOT=/workspace/android-diagnostic/sdk \
  timeout 45 /workspace/android-diagnostic/sdk/emulator/emulator \
  -avd storm_api35_arm64 -no-window -no-audio -no-snapshot \
  -accel off -gpu swiftshader -verbose
```

Evidence: [launch log](evidence/android-local-arm64-2026-10-15/arm64-launch.log), [successful AVD creation](evidence/android-local-arm64-2026-10-15/avd-create.log), [AVD configuration](evidence/android-local-arm64-2026-10-15/avd-config.ini), [emulator version](evidence/android-local-arm64-2026-10-15/emulator-version.log), [acceleration probe](evidence/android-local-arm64-2026-10-15/accel-check.log), [input verification](evidence/android-local-arm64-2026-10-15/input.json), [image metadata](evidence/android-local-arm64-2026-10-15/image-source.properties), [SDK download metadata](evidence/android-local-arm64-2026-10-15/sdk-download-metadata.json).

**Outcome:** original APK installation and execution remain unverified in this continuation because no compatible Android guest could boot. This does not reproduce or explain Xiaomi's `package is null`. No code fix or replacement prerelease is justified by these findings. No GitHub emulator workflow was launched. Further native testing needs a host supported by the official ARM64 emulator (for example Apple silicon with its official emulator), or an accessible ARM64 Android device; Xiaomi-specific diagnosis still needs that phone's PackageManager/ADB error.
