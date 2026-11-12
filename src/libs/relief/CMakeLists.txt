# Relief preparation (README.md): DeepBump through onnxruntime at game import. Not part of libengine: on Android it is
# libstorm_relief.so for the launcher process (NativeRelief), on the host the storm_relief_cli tool.
include(FetchContent)

# onnxruntime 1.31.1 (MIT), Microsoft's prebuilt release: the Android AAR (Maven Central) for its libonnxruntime.so and
# C headers, the Linux x64 archive (GitHub release) on the host. Licence and notices: third_party/licenses/onnxruntime-*.
if(ANDROID)
    storm_fetch_declare(storm_onnxruntime
        URL https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.31.1/onnxruntime-android-1.31.1.aar
        URL_HASH SHA256=d4d50660300ed2c4f38ffdfd5ab5bfa3a22f995f282dda24f42ea3ea55467b49
        DOWNLOAD_NAME onnxruntime-android-1.31.1.zip
        STORM_POPULATE_ONLY)
else()
    storm_fetch_declare(storm_onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-linux-x64-1.31.1.tgz
        URL_HASH SHA256=c849e36a1ef3e22d4ff217b9d1ce4a3cf5b9467b975610913ae959ad5b16bdc9
        STORM_POPULATE_ONLY)
endif()
# DeepBump (GPL-3.0) at the commit whose licence and weights were checked: deepbump256.onnx and LICENSE.
# Provenance: third_party/licenses/DeepBump-PROVENANCE.txt.
storm_fetch_declare(storm_deepbump
    URL https://codeload.github.com/HugoTini/DeepBump/tar.gz/fad19ba87daed12b1d0410a57e74f3d79e82f78d
    URL_HASH SHA256=213f64f9f38811ae67ef4bb16d38d851bb5c8af2efc2e368b445ab809b38262a
    STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_onnxruntime storm_deepbump)

set(STORM_DEEPBUMP_MODEL "${storm_deepbump_SOURCE_DIR}/deepbump256.onnx")
file(SHA256 "${STORM_DEEPBUMP_MODEL}" model_digest)
if(NOT model_digest STREQUAL "3a2ababc5fa652b19040a0f3d639b71826368e52ad805cf70ffb0e7c96a54d67")
    message(FATAL_ERROR "deepbump256.onnx does not match the checked model (sha256 ${model_digest})")
endif()

add_library(storm_onnxruntime SHARED IMPORTED GLOBAL)
if(ANDROID)
    set(onnxruntime_library "${storm_onnxruntime_SOURCE_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so")
    set_target_properties(storm_onnxruntime PROPERTIES IMPORTED_LOCATION "${onnxruntime_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${storm_onnxruntime_SOURCE_DIR}/headers")
else()
    set(onnxruntime_library "${storm_onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.1.31.1")
    set_target_properties(storm_onnxruntime PROPERTIES IMPORTED_LOCATION "${onnxruntime_library}"
        IMPORTED_SONAME libonnxruntime.so.1 INTERFACE_INCLUDE_DIRECTORIES "${storm_onnxruntime_SOURCE_DIR}/include")
endif()

add_library(storm_relief_core STATIC prepare.cpp relief.hpp)
target_compile_features(storm_relief_core PUBLIC cxx_std_20)
target_include_directories(storm_relief_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(storm_relief_core PUBLIC storm_bgfx_fx_core storm_texture_core storm_onnxruntime)
if(ANDROID)
    target_link_libraries(storm_relief_core PUBLIC log)
endif()
set_target_properties(storm_relief_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# What the app packages: the library, onnxruntime and the model (an asset), next to libengine.so in <build>/game.
if(ANDROID)
    add_library(storm_relief SHARED jni.cpp)
    target_link_libraries(storm_relief PRIVATE storm_relief_core android)
    target_link_options(storm_relief PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384")
    set_target_properties(storm_relief PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
    include(build_stamp)
    storm_build_stamp(storm_relief) # build_apk.py refuses a library of another engine commit
    add_custom_command(TARGET storm_relief POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${onnxruntime_library}" "${CMAKE_BINARY_DIR}/game/libonnxruntime.so"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/game/assets"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STORM_DEEPBUMP_MODEL}" "${CMAKE_BINARY_DIR}/game/assets/deepbump256.onnx")
endif()
# The same job from a shell: host checks, and timing on a device or emulator (adb push with libonnxruntime.so).
add_executable(storm_relief_cli cli.cpp)
target_link_libraries(storm_relief_cli PRIVATE storm_relief_core)
if(NOT ANDROID)
    set_target_properties(storm_relief_cli PROPERTIES BUILD_RPATH "${storm_onnxruntime_SOURCE_DIR}/lib")
    # tests/renderer_bgfx/run.py: the preparation with the real model on a small synthetic game.
    add_executable(storm_relief_check tests/relief_check.cpp)
    target_link_libraries(storm_relief_check PRIVATE storm_relief_core)
    set_target_properties(storm_relief_check PROPERTIES BUILD_RPATH "${storm_onnxruntime_SOURCE_DIR}/lib")
    # The model's path for the runner (not a compile definition: absolute paths there defeat ccache).
    file(WRITE "${CMAKE_BINARY_DIR}/deepbump_model_path.txt" "${STORM_DEEPBUMP_MODEL}")
endif()
