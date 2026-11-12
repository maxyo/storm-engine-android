# Android Frame Pacing (Swappy GL) from the Android Game SDK (AGDK), Apache-2.0
# (third_party/licenses/Swappy-Apache-2.0.txt). The prebuilt static library of Google's
# androidx.games:games-frame-pacing AAR (Google Maven), pinned by version and SHA-256; the AAR is a
# zip that also carries the Java helper (SwappyDisplayManager) as a dex embedded in the static library,
# so the application needs no Java part. Built for c++_shared, API 21, like the engine's libraries.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_SWAPPY=/path/to/the/extracted/aar
# Linked by storm_bgfx (src/shim/frame_pacing.cpp) only: libengine.so has the one Swappy instance.
set(storm_swappy_version 2.1.3)
storm_fetch_declare(storm_swappy
    URL "https://dl.google.com/android/maven2/androidx/games/games-frame-pacing/${storm_swappy_version}/games-frame-pacing-${storm_swappy_version}.aar"
    URL_HASH SHA256=a9f83e0cf47d694ed2cea0ae995d5cf7411060d9d4b680610430d03d6b20ea27
    DOWNLOAD_NAME "games-frame-pacing-${storm_swappy_version}.zip"
    STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_swappy)
set(storm_swappy_module "${storm_swappy_SOURCE_DIR}/prefab/modules/swappy_static")
set(storm_swappy_library "${storm_swappy_module}/libs/android.${ANDROID_ABI}/libswappy_static.a")
if(NOT EXISTS "${storm_swappy_library}")
    message(FATAL_ERROR "Swappy ${storm_swappy_version} has no static library for ${ANDROID_ABI}: ${storm_swappy_library}")
endif()
add_library(storm_swappy STATIC IMPORTED GLOBAL)
set_target_properties(storm_swappy PROPERTIES IMPORTED_LOCATION "${storm_swappy_library}")
target_link_libraries(storm_swappy INTERFACE android log EGL GLESv3)
set(storm_swappy_include "${storm_swappy_module}/include")
