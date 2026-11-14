# Portable game UI runtime: RmlUi documents drawn through the bgfx render backend (inside the
# engine frame for the game, on a bgfx context of its own elsewhere). Stand-alone project for the
# host checks and captures; on Android it is part of the engine build, which makes the APK's
# libstorm_ui.so next to libengine.so and libbgfx.so (one bgfx per process).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(storm-ui LANGUAGES C CXX)
    if(ANDROID)
        message(FATAL_ERROR "The Android UI runtime is built by the engine build (tools/android/ci_engine.sh), with the libbgfx.so it shares with libengine.so")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormBuildCache.cmake")
    set(STORM_ENABLE_UI ON)
endif()
option(STORM_ENABLE_UI "Build the RmlUi based portable game UI runtime" OFF)
if(NOT STORM_ENABLE_UI)
    return()
endif()
# Inside the engine build: neither this library nor its third-party sources take the engine's
# global flags (-std=c++20 for C sources, the Windows type headers ...).
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "" COMPILE_DEFINITIONS "" COMPILE_OPTIONS "")

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
# Local fixes on top of the RmlUi pin: patches/rmlui-*.patch, applied in the build directory's own patched view of
# the shared source copy (storm_patched_view): a changed patch recompiles only what the changed files reach.
file(GLOB storm_ui_rmlui_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/rmlui-*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${storm_ui_rmlui_patches})
storm_fetch_declare(storm_ui_rmlui
    URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
    URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede
    STORM_PATCHES . ${storm_ui_rmlui_patches})

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
# Builds with debug info (the engine's Android builds: -g) keep only line tables for the UI
# runtime and its third-party code: crash stacks still resolve to function, file and line, and
# libstorm_ui.so ships unstripped in debug APKs without full variable/type info (about 4x larger).
string(TOUPPER "${CMAKE_BUILD_TYPE}" storm_ui_config)
if(" ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${storm_ui_config}} " MATCHES " -g ")
    set(storm_ui_line_tables_only ON)
endif()
function(storm_ui_line_tables)
    if(storm_ui_line_tables_only)
        foreach(target IN LISTS ARGN)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -gline-tables-only)
            endif()
        endforeach()
    endif()
endfunction()
storm_ui_line_tables(rmlui_core rmlui_debugger freetype lunasvg plutovg)
# Licence texts of the pinned dependencies (<build>/licenses), packaged with libstorm_ui.so.
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

# bgfx is pinned only by src/libs/renderer_bgfx. A stand-alone build pulls that directory in: it
# pins and configures bgfx (limits, GLES 3.0) and provides the shader build (the cached host
# shaderc). Inside the engine it is there already.
set(storm_ui_renderer_bgfx "${CMAKE_CURRENT_SOURCE_DIR}/../renderer_bgfx")
if(NOT COMMAND storm_bgfx_embed_shaders)
    add_subdirectory("${storm_ui_renderer_bgfx}" "${CMAKE_CURRENT_BINARY_DIR}/renderer_bgfx" EXCLUDE_FROM_ALL)
endif()

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
    src/screens/menu_quick_save.cpp
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
    src/touch_motion.cpp
    src/draw_elements.cpp
    src/icons.cpp
    src/game_image.cpp
    src/picture_atlas.cpp
    src/frame_renderer.cpp)
target_compile_features(storm_ui PUBLIC cxx_std_20)
# The finger physics shared with the game cameras (header-only, standard library only).
target_include_directories(storm_ui PUBLIC include PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../util/include")
target_link_libraries(storm_ui PUBLIC RmlUi::RmlUi nlohmann_json::nlohmann_json PRIVATE lunasvg::lunasvg plutovg::plutovg)
if(ANDROID)
    target_sources(storm_ui PRIVATE src/asset_files.cpp) # the pack in the APK assets
    target_link_libraries(storm_ui PRIVATE android)
endif()

# The render backend: bgfx. Only the frame owner's interface header is used (no symbols), so the
# stand-alone build does not need the engine's renderer library.
file(GLOB storm_ui_shader_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.sc")
list(FILTER storm_ui_shader_sources EXCLUDE REGEX "varying\\.def\\.sc$")
storm_bgfx_embed_shaders(TARGET storm_ui_shaders NAMESPACE storm_ui_shaders
    VARYING "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc" SOURCES ${storm_ui_shader_sources})
add_library(storm_ui_bgfx STATIC src/backend_bgfx.cpp)
target_include_directories(storm_ui_bgfx PUBLIC "${storm_ui_renderer_bgfx}/include")
target_link_libraries(storm_ui_bgfx PUBLIC storm_ui storm_bgfx_runtime PRIVATE storm_ui_shaders)

if(ANDROID)
    # libstorm_ui.so, the one UI runtime of the APK: the game (src/libs/ui_host in libengine.so)
    # and the application screens without the engine (launcher, downloads, gallery: their Java
    # view owns the EGL context, android/jni_host.cpp) both load it. It takes bgfx from
    # libbgfx.so (storm_bgfx_runtime), so the game process has one bgfx. Built by the engine's
    # Android build, which packages it with libengine.so and libbgfx.so.
    add_library(storm_ui_runtime SHARED android/jni_host.cpp)
    target_link_libraries(storm_ui_runtime PRIVATE -Wl,--whole-archive storm_ui storm_ui_bgfx -Wl,--no-whole-archive android log EGL
        PUBLIC storm_bgfx_runtime)
    target_link_options(storm_ui_runtime PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    set_target_properties(storm_ui_runtime PROPERTIES OUTPUT_NAME storm_ui LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
    # Users compile against the runtime's headers (and RmlUi's, nlohmann::json's) but link only it.
    target_include_directories(storm_ui_runtime INTERFACE "$<TARGET_PROPERTY:storm_ui_bgfx,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(storm_ui_runtime INTERFACE "$<TARGET_PROPERTY:storm_ui_bgfx,INTERFACE_COMPILE_DEFINITIONS>")
    target_compile_features(storm_ui_runtime INTERFACE cxx_std_20)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/build_stamp.cmake")
    storm_build_stamp(storm_ui_runtime)
    storm_ui_line_tables(storm_ui storm_ui_bgfx storm_ui_runtime)
elseif(NOT CMAKE_CROSSCOMPILING)
    # Headless captures for design checks: bgfx GLES 3.0 on a surfaceless EGL display, PNG output.
    # .tx decoding for the capture tool's pictures (the game takes them from the renderer).
    add_library(storm_ui_tx STATIC src/tx_texture.cpp)
    target_link_libraries(storm_ui_tx PUBLIC storm_ui PRIVATE storm_bgfx_runtime)
    add_executable(storm_ui_shot tools/ui_shot.cpp)
    target_link_libraries(storm_ui_shot PRIVATE storm_ui_bgfx storm_ui_tx plutovg::plutovg ${CMAKE_DL_LIBS})
    add_executable(storm_ui_unit tests/unit_check.cpp)
    target_link_libraries(storm_ui_unit PRIVATE storm_ui storm_ui_tx)
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
    # A new picture is shown whole or not yet (RenderBackend::FrameHeld), through the bgfx backend: six
    # panels with a box shadow each appear, with too few views to make their textures beside the frame.
    add_test(NAME ui_whole_frame COMMAND storm_ui_shot --pack "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack" --doc panels.rml --size 640x360 --density 1
             --views 12 --out "${CMAKE_CURRENT_BINARY_DIR}/whole-frame.png"
             --script "wait 0.2; mark; props panels {\"sizes\":[40,50,60,70,80,90]}; whole; wait 0.0166; whole; wait 0.0166; whole; wait 0.3; arrived")
    add_executable(storm_ui_modals_unit tests/modals_check.cpp)
    target_link_libraries(storm_ui_modals_unit PRIVATE storm_ui)
    add_test(NAME ui_modals COMMAND storm_ui_modals_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/models" "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack"
        "${storm_ui_rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf")
    add_executable(storm_ui_routing_unit tests/routing_check.cpp)
    target_link_libraries(storm_ui_routing_unit PRIVATE storm_ui)
    add_test(NAME ui_routing COMMAND storm_ui_routing_unit "${CMAKE_CURRENT_SOURCE_DIR}/tests/states" "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
endif()
