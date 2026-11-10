# Sourced by the Android build scripts: STORM_ANDROID_ABI (default arm64-v8a; x86_64 for emulator
# debug builds, never for releases) -> abi, arch (cmake/toolchains/android-<arch>.cmake), triple
# (NDK compiler prefix) and sfx: "" for arm64, "-<arch>" otherwise, appended to every default
# build/prefix directory so that the warm arm64 directories stay untouched.
abi=${STORM_ANDROID_ABI:-arm64-v8a}
case "$abi" in
  arm64-v8a) arch=arm64 triple=aarch64-linux-android sfx= ;;
  x86_64) arch=x86_64 triple=x86_64-linux-android sfx=-x86_64 ;;
  *) echo "Unsupported STORM_ANDROID_ABI: $abi (arm64-v8a or x86_64)" >&2; exit 2 ;;
esac
