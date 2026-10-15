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

To complete emulator UI checks reliably, use a working accelerated emulator or explicitly authorize narrowly scoped KVM access on the disposable Actions runner. The task explicitly prohibited changing KVM permissions without confirmation; no such change was made. A real ARM64 device remains required for engine/Vulkan/audio validation. Do not label a new release installation-fixed until an identified failure is reproduced and the replacement passes the relevant installation test.
