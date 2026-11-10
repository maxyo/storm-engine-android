# Portable game UI runtime: RmlUi documents drawn by a GLES 3 / GL 3.3 context that the
# platform host owns. Stand-alone project (host checks) or part of the Android build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(storm-ui LANGUAGES C CXX)
    set(STORM_ENABLE_UI ON)
endif()
option(STORM_ENABLE_UI "Build the RmlUi based portable game UI runtime" OFF)
if(NOT STORM_ENABLE_UI)
    return()
endif()

include(FetchContent)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Pinned release archives. Update the version and the hash together.
FetchContent_Declare(storm_ui_freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
    URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261)
FetchContent_Declare(storm_ui_lunasvg
    URL https://github.com/sammycage/lunasvg/archive/refs/tags/v3.5.0.tar.gz
    URL_HASH SHA256=1abf1472ee6c4d19797916e8cc3c2e4b628e0d81178ffac60bdb0d457e32c690)
FetchContent_Declare(storm_ui_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_Declare(storm_ui_rmlui
    URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
    URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede)

# FreeType without optional codecs: the bundled fonts are plain TrueType outlines.
foreach(codec ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${codec} TRUE CACHE BOOL "" FORCE)
endforeach()
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "" FORCE)
set(PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD ON CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(storm_ui_freetype storm_ui_lunasvg storm_ui_json)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(storm_ui_rmlui)

add_library(storm_ui STATIC
    src/ui_host.cpp
    src/interface_model.cpp
    src/screens/router.cpp
    src/screens/controls.cpp
    src/screens/registry.cpp
    src/screens/generic.cpp
    src/screens/trade_commerce.cpp
    src/screens/trade_crew.cpp
    src/screens/trade_shipyard.cpp
    src/screens/dialogue.cpp
    src/screens/menu_game.cpp
    src/screens/menu_new_game.cpp
    src/screens/menu_saves.cpp
    src/screens/menu_settings.cpp
    src/screens/menu_title.cpp
    src/screens/menu_game_over.cpp
    src/hud/hud.cpp
    src/hud/states.cpp
    src/hud/land.cpp
    src/template.cpp
    src/components.cpp
    src/behaviors.cpp
    src/draw_elements.cpp
    src/icons.cpp
    src/tx_texture.cpp
    src/game_image.cpp
    src/picture_atlas.cpp
    src/backend_gl3.cpp
    "${storm_ui_rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp")
target_compile_features(storm_ui PUBLIC cxx_std_20)
target_include_directories(storm_ui PUBLIC include PRIVATE src "${storm_ui_rmlui_SOURCE_DIR}/Backends")
target_link_libraries(storm_ui PUBLIC RmlUi::RmlUi nlohmann_json::nlohmann_json PRIVATE lunasvg::lunasvg plutovg::plutovg)

if(ANDROID)
    # RmlUi's GL3 renderer picks GLSL ES 3.20 on Android. Its WebGL 2 path is the same code
    # with GLSL ES 3.00 and <GLES3/gl3.h>, which every GLES 3.0 device (and the emulator's
    # SwiftShader) accepts; select it for that one file.
    set_source_files_properties("${storm_ui_rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp"
        PROPERTIES COMPILE_DEFINITIONS RMLUI_PLATFORM_EMSCRIPTEN)
    # The Java host owns the EGL context (GLSurfaceView); this library only issues GLES calls.
    add_library(storm_ui_android SHARED android/jni_host.cpp android/asset_files.cpp)
    target_link_libraries(storm_ui_android PRIVATE storm_ui android log GLESv3)
    target_link_options(storm_ui_android PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    set_target_properties(storm_ui_android PROPERTIES OUTPUT_NAME storm_ui)
    # The APK ships the stripped copy; the unstripped one stays for symbolication.
    add_custom_command(TARGET storm_ui_android POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/package"
        COMMAND ${CMAKE_STRIP} --strip-unneeded -o "${CMAKE_CURRENT_BINARY_DIR}/package/libstorm_ui.so" "$<TARGET_FILE:storm_ui_android>"
        VERBATIM)
elseif(NOT CMAKE_CROSSCOMPILING)
    # Headless host renderer for design checks: EGL pbuffer + desktop GL 3.3, PNG output.
    add_executable(storm_ui_shot tools/ui_shot.cpp)
    target_link_libraries(storm_ui_shot PRIVATE storm_ui plutovg::plutovg ${CMAKE_DL_LIBS})
    add_executable(storm_ui_unit tests/unit_check.cpp)
    target_link_libraries(storm_ui_unit PRIVATE storm_ui)
    enable_testing()
    add_test(NAME ui_unit COMMAND storm_ui_unit)
    add_executable(storm_ui_screens_unit tests/screens_check.cpp)
    target_link_libraries(storm_ui_screens_unit PRIVATE storm_ui)
    add_test(NAME ui_screens COMMAND storm_ui_screens_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    add_executable(storm_ui_menus_unit tests/menus_check.cpp)
    target_link_libraries(storm_ui_menus_unit PRIVATE storm_ui)
    add_test(NAME ui_menus COMMAND storm_ui_menus_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    add_executable(storm_ui_hud_unit tests/hud_check.cpp)
    target_link_libraries(storm_ui_hud_unit PRIVATE storm_ui)
    add_test(NAME ui_hud COMMAND storm_ui_hud_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states")
    add_executable(storm_ui_hud_land_unit tests/hud_land_check.cpp)
    target_link_libraries(storm_ui_hud_land_unit PRIVATE storm_ui)
    add_test(NAME ui_hud_land COMMAND storm_ui_hud_land_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states" "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
endif()
