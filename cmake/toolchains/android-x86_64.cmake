# Storm Android toolchain, x86_64: debug builds for x86_64 emulators only (they run natively
# instead of through ARM translation). Release APKs are arm64-v8a only.
set(STORM_ANDROID_TOOLCHAIN_ABI x86_64)
include("${CMAKE_CURRENT_LIST_DIR}/android-common.cmake")
