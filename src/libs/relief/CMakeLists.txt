# Relief preparation (README.md): DeepBump through onnxruntime. The game makes its maps with it while it runs
# (renderer_bgfx effects/relief_generator); storm_relief_cli does a whole game at once.
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
FetchContent_MakeAvailable(storm_onnxruntime)

# The DeepBump model (GPL-3.0), derived from HugoTini/DeepBump at fad19ba8 by model/make_model.py (model/README.md).
set(STORM_DEEPBUMP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/model/deepbump256-int8.onnx")
file(SHA256 "${STORM_DEEPBUMP_MODEL}" model_digest)
if(NOT model_digest STREQUAL "24b0975701c94500a7ecbc1e0927fc0d07803c19310437ddc4b4ca86fe239603")
    message(FATAL_ERROR "model/deepbump256-int8.onnx does not match its recorded sha256 (${model_digest})")
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

# What the app packages next to libengine.so (which links onnxruntime): onnxruntime and the model (an asset), in
# <build>/game.
if(ANDROID)
    add_custom_target(storm_relief_package
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/game/assets"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${onnxruntime_library}" "${CMAKE_BINARY_DIR}/game/libonnxruntime.so"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STORM_DEEPBUMP_MODEL}" "${CMAKE_BINARY_DIR}/game/assets/deepbump256-int8.onnx"
        VERBATIM)
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
