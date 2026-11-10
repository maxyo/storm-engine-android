# Portable game UI runtime: RmlUi documents drawn by a GLES 3 / GL 3.3 context that the
# platform host owns. Stand-alone project (host checks) or part of the Android build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(storm-ui LANGUAGES C CXX)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormBuildCache.cmake")
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
storm_fetch_declare(storm_ui_freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
    URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261)
storm_fetch_declare(storm_ui_lunasvg
    URL https://github.com/sammycage/lunasvg/archive/refs/tags/v3.5.0.tar.gz
    URL_HASH SHA256=1abf1472ee6c4d19797916e8cc3c2e4b628e0d81178ffac60bdb0d457e32c690)
storm_fetch_declare(storm_ui_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
storm_fetch_declare(storm_ui_rmlui
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
# Licence texts of the pinned dependencies, packaged next to libstorm_ui.so.
foreach(notice
        "${storm_ui_rmlui_SOURCE_DIR}/LICENSE.txt|RmlUi-MIT.txt"
        "${storm_ui_freetype_SOURCE_DIR}/docs/FTL.TXT|FreeType-FTL.txt"
        "${storm_ui_lunasvg_SOURCE_DIR}/LICENSE|lunasvg-MIT.txt"
        "${storm_ui_lunasvg_SOURCE_DIR}/plutovg/LICENSE|plutovg-MIT.txt"
        "${storm_ui_json_SOURCE_DIR}/LICENSE.MIT|nlohmann-json-MIT.txt")
    string(REPLACE "|" ";" notice "${notice}")
    list(GET notice 0 from)
    list(GET notice 1 to)
    configure_file("${from}" "${CMAKE_BINARY_DIR}/licenses/${to}" COPYONLY)
endforeach()

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
    src/screens/hero.cpp
    src/screens/hero_character.cpp
    src/screens/hero_items.cpp
    src/screens/hero_ship.cpp
    src/screens/hero_journal.cpp
    src/screens/hero_nations.cpp
    src/screens/dialogue.cpp
    src/screens/menu_game.cpp
    src/screens/menu_new_game.cpp
    src/screens/menu_saves.cpp
    src/screens/menu_settings.cpp
    src/screens/menu_title.cpp
    src/screens/extra_games.cpp
    src/screens/extra_repair.cpp
    src/screens/extra_transfer.cpp
    src/screens/extra_alchemy.cpp
    src/screens/extra_goods.cpp
    src/screens/extra_sails.cpp
    src/screens/menu_game_over.cpp
    src/screens/maps.cpp
    src/screens/reading.cpp
    src/screens/rest.cpp
    src/screens/hero_tradebook.cpp
    src/screens/launcher.cpp
    src/hud/hud.cpp
    src/hud/states.cpp
    src/hud/land.cpp
    src/hud/sea.cpp
    src/hud/world_map.cpp
    src/hud/spyglass.cpp
    src/template.cpp
    src/components.cpp
    src/behaviors.cpp
    src/draw_elements.cpp
    src/icons.cpp
    src/tx_texture.cpp
    src/game_image.cpp
    src/picture_atlas.cpp
    src/backend_gl3.cpp
    src/frame_renderer.cpp
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
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/build_stamp.cmake")
    storm_build_stamp(storm_ui_android)
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
    add_executable(storm_ui_rare_unit tests/rare_check.cpp)
    target_link_libraries(storm_ui_rare_unit PRIVATE storm_ui)
    add_test(NAME ui_rare COMMAND storm_ui_rare_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    add_executable(storm_ui_hud_unit tests/hud_check.cpp)
    target_link_libraries(storm_ui_hud_unit PRIVATE storm_ui)
    add_test(NAME ui_hud COMMAND storm_ui_hud_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states")
    add_executable(storm_ui_hud_spyglass_unit tests/hud_spyglass_check.cpp)
    target_link_libraries(storm_ui_hud_spyglass_unit PRIVATE storm_ui)
    add_test(NAME ui_hud_spyglass COMMAND storm_ui_hud_spyglass_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states")
    add_executable(storm_ui_press_unit tests/press_check.cpp)
    target_link_libraries(storm_ui_press_unit PRIVATE storm_ui)
    add_test(NAME ui_press COMMAND storm_ui_press_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack"
        "${storm_ui_rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf")
    add_executable(storm_ui_slot_unit tests/slot_check.cpp)
    target_link_libraries(storm_ui_slot_unit PRIVATE storm_ui)
    add_test(NAME ui_slot COMMAND storm_ui_slot_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
    add_executable(storm_ui_hud_land_unit tests/hud_land_check.cpp)
    target_link_libraries(storm_ui_hud_land_unit PRIVATE storm_ui)
    add_test(NAME ui_hud_land COMMAND storm_ui_hud_land_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states" "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
    add_executable(storm_ui_hud_sea_unit tests/hud_sea_check.cpp)
    target_link_libraries(storm_ui_hud_sea_unit PRIVATE storm_ui)
    add_executable(storm_ui_hud_worldmap_unit tests/hud_worldmap_check.cpp)
    target_link_libraries(storm_ui_hud_worldmap_unit PRIVATE storm_ui)
    add_test(NAME ui_hud_worldmap COMMAND storm_ui_hud_worldmap_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states")
    add_executable(storm_ui_frame_cost_unit tests/frame_cost_check.cpp)
    target_link_libraries(storm_ui_frame_cost_unit PRIVATE storm_ui)
    target_include_directories(storm_ui_frame_cost_unit PRIVATE src)
    add_test(NAME ui_frame_cost COMMAND storm_ui_frame_cost_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states" "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
    add_test(NAME ui_hud_sea COMMAND storm_ui_hud_sea_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states" "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
endif()
