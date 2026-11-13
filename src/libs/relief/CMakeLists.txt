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

# PBRify's normal, height and roughness models (CC0-1.0, model/README.md): not in the repository (the owner's rule). The
# float models are fetched by their pins; model/make_models.py quantises them to int8 at build time, once per machine
# (storm_derived_files, cmake/StormDerivedFiles.cmake), and the results must be byte for byte the pinned ones. The maps'
# method includes those digests (fx/relief_model.hpp): a new model must change them.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormDerivedFiles.cmake")
set(storm_pbrify_revision b35b7f4c924936803bf79e868302d7f8f643d302)
set(storm_relief_sources "")
set(storm_relief_inputs "")
foreach(pair "normal|1x-PBRify_NormalV3.onnx|19d78f555f994219e2692876ab0fb6c3e4382503952dbe72d74db89d24870ba4"
             "height|1x-PBRify_Height.onnx|2b14afd583148cb5b0a554bf86a009b6a898aadc00d213aa3ec2544e17c835f5"
             "roughness|1x-PBRify_RoughnessV2.onnx|8a8de1bbeab70df8b2550bdc11fddd5a315a12eeb47d561ce55b38551ea52131")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 model)
    list(GET fields 1 file)
    list(GET fields 2 sha256)
    # The ONNX exports of PBRify_Remix's models by fernandotonon/QtMeshEditor-models (third_party/licenses/PBRify-*).
    storm_fetch_declare(storm_pbrify_${model}
        URL https://huggingface.co/fernandotonon/QtMeshEditor-models/resolve/${storm_pbrify_revision}/${file}
        URL_HASH SHA256=${sha256}
        DOWNLOAD_NO_EXTRACT TRUE
        STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_pbrify_${model})
    list(APPEND storm_relief_sources "${storm_pbrify_${model}_SOURCE_DIR}")
    list(APPEND storm_relief_inputs "${sha256}")
endforeach()
set(storm_relief_outputs
    "pbrify-normal-int8.onnx|kReliefNormalModelDigest|fc91ce30e48deaf5847ed19342bd1fc7a8ffc11a2ec94f26c4d6bcbe63df4b33"
    "pbrify-height-int8.onnx|kReliefHeightModelDigest|01f251dc40e79fa7ef5348b51f538de5a14bc6113c8ea3173ba78ce0b648c409"
    "pbrify-roughness-int8.onnx|kReliefRoughnessModelDigest|860845c92142b7d1bd89ac514ae58206d431fd2c899e955dd492348389c5f00b")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../renderer_bgfx/fx/include/storm/fx/relief_model.hpp" relief_model_header)
set(storm_relief_pins "")
foreach(pair ${storm_relief_outputs})
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 file)
    list(GET fields 1 constant)
    list(GET fields 2 expected)
    string(REGEX MATCH "${constant} = 0x([0-9a-f]+)ull" digest_line "${relief_model_header}")
    string(SUBSTRING "${expected}" 0 16 prefix)
    if(NOT CMAKE_MATCH_1 STREQUAL prefix)
        message(FATAL_ERROR "fx::${constant} (${CMAKE_MATCH_1}) is not the sha256 prefix of ${file} (${prefix})")
    endif()
    list(APPEND storm_relief_pins "${file}=${expected}")
endforeach()
storm_derived_files(storm_relief_models storm_relief_models
    SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/model/make_models.py"
    REQUIREMENTS "${CMAKE_CURRENT_SOURCE_DIR}/model/requirements.txt"
    SOURCES ${storm_relief_sources}
    INPUTS ${storm_relief_inputs}
    OUTPUTS ${storm_relief_pins})
set(STORM_RELIEF_MODELS "${storm_relief_models_DIR}")

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
    add_dependencies(storm_relief_package storm_relief_models)
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
    add_dependencies(storm_relief_check storm_relief_models)
    # The models' directory for the runner (not a compile definition: absolute paths there defeat ccache).
    file(WRITE "${CMAKE_BINARY_DIR}/relief_models_path.txt" "${STORM_RELIEF_MODELS}")
endif()
