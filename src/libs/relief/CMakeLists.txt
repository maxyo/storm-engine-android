# Relief preparation (README.md): PBRify's models through onnxruntime. The game makes its maps with them while it runs
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

# PBRify's normal and height models (CC0-1.0), int8 from Kim2091/PBRify_Remix's by model/make_models.py (model/README.md).
# The maps' method includes their digests (fx/relief_model.hpp): a new model must change them.
set(STORM_RELIEF_MODELS "${CMAKE_CURRENT_SOURCE_DIR}/model")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../renderer_bgfx/fx/include/storm/fx/relief_model.hpp" relief_model_header)
foreach(pair "pbrify-normal-int8.onnx|kReliefNormalModelDigest|fc91ce30e48deaf5847ed19342bd1fc7a8ffc11a2ec94f26c4d6bcbe63df4b33"
             "pbrify-height-int8.onnx|kReliefHeightModelDigest|01f251dc40e79fa7ef5348b51f538de5a14bc6113c8ea3173ba78ce0b648c409"
             "pbrify-roughness-int8.onnx|kReliefRoughnessModelDigest|860845c92142b7d1bd89ac514ae58206d431fd2c899e955dd492348389c5f00b")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 file)
    list(GET fields 1 constant)
    list(GET fields 2 expected)
    file(SHA256 "${STORM_RELIEF_MODELS}/${file}" digest)
    if(NOT digest STREQUAL expected)
        message(FATAL_ERROR "model/${file} does not match its recorded sha256 (${digest})")
    endif()
    string(REGEX MATCH "${constant} = 0x([0-9a-f]+)ull" digest_line "${relief_model_header}")
    string(SUBSTRING "${digest}" 0 16 prefix)
    if(NOT CMAKE_MATCH_1 STREQUAL prefix)
        message(FATAL_ERROR "fx::${constant} (${CMAKE_MATCH_1}) is not the sha256 prefix of model/${file} (${prefix})")
    endif()
endforeach()

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

# What the app packages next to libengine.so (which links onnxruntime): onnxruntime and the three models (assets), in
# <build>/game.
if(ANDROID)
    add_custom_target(storm_relief_package
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/game/assets"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${onnxruntime_library}" "${CMAKE_BINARY_DIR}/game/libonnxruntime.so"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STORM_RELIEF_MODELS}/pbrify-normal-int8.onnx" "${CMAKE_BINARY_DIR}/game/assets/pbrify-normal-int8.onnx"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STORM_RELIEF_MODELS}/pbrify-height-int8.onnx" "${CMAKE_BINARY_DIR}/game/assets/pbrify-height-int8.onnx"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STORM_RELIEF_MODELS}/pbrify-roughness-int8.onnx" "${CMAKE_BINARY_DIR}/game/assets/pbrify-roughness-int8.onnx"
        VERBATIM)
endif()
# The same job from a shell: host checks, and timing on a device or emulator (adb push with libonnxruntime.so).
add_executable(storm_relief_cli cli.cpp)
target_link_libraries(storm_relief_cli PRIVATE storm_relief_core)
if(NOT ANDROID)
    set_target_properties(storm_relief_cli PROPERTIES BUILD_RPATH "${storm_onnxruntime_SOURCE_DIR}/lib")
    # tests/renderer_bgfx/run.py: the preparation with the real models on a small synthetic game.
    add_executable(storm_relief_check tests/relief_check.cpp)
    target_link_libraries(storm_relief_check PRIVATE storm_relief_core)
    set_target_properties(storm_relief_check PROPERTIES BUILD_RPATH "${storm_onnxruntime_SOURCE_DIR}/lib")
    # The models' directory for the runner (not a compile definition: absolute paths there defeat ccache).
    file(WRITE "${CMAKE_BINARY_DIR}/relief_models_path.txt" "${STORM_RELIEF_MODELS}")
endif()
