# Host preview and regression harness for the bgfx graphics effects package
# (src/libs/renderer_bgfx/fx). Standalone project: it does not configure the
# engine. See README.md.
cmake_minimum_required(VERSION 3.21)
project(storm-fx-preview LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
include(FetchContent)

set(STORM_FX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/libs/renderer_bgfx/fx")

# The same pinned, patched bgfx as the game (renderer_bgfx/cmake/bgfx_pin.cmake, through
# bgfx_upstream.cmake) and the shared host shaderc of that pin (storm_shaderc.cmake).
set(STORM_BGFX_CMAKE "${STORM_FX_DIR}/../cmake")
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
include("${STORM_BGFX_CMAKE}/bgfx_upstream.cmake")
include("${STORM_BGFX_CMAKE}/storm_shaderc.cmake")
# Same backend as the game on Android: OpenGL ES 3.0 (headless EGL pbuffer here).
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGLES=30)
target_link_libraries(bgfx PUBLIC EGL GLESv2)
target_compile_options(bimg_encode PRIVATE -w)
# What the effects package links (renderer_bgfx/CMakeLists.txt defines it in the engine).
add_library(storm_bgfx_runtime INTERFACE)
target_link_libraries(storm_bgfx_runtime INTERFACE bgfx bimg bx)

set(STORM_FX_BGFX_SHADER_INCLUDE "${STORM_BGFX_SOURCE_DIR}/bgfx/src")
set(STORM_FX_SHADERC_COMMAND "${STORM_SHADERC_COMMAND}")
set(STORM_FX_SHADERC_DEPENDS "${STORM_SHADERC_DEPENDS}")
enable_testing()
add_subdirectory("${STORM_FX_DIR}" fx)

# The tool's own shaders: a fixed-function stand-in that includes fx_lighting.sh
# the way the emulator will. Built with the same rules as the package shaders.
set(preview_sc "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(preview_shaders)
# One vertex + fragment pair per lighting variant the shots use; the define lists are what
# fx::LightingDefines() returns for those settings, with + for ; (main.cpp looks the pair up by that string).
set(preview_variants
    "legacy|"
    "pp|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE"
    "spec|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT"
    "medium|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SHADOW+FX_LIGHT_RIM+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=4+FX_LIGHT_NORMALMAP+FX_LIGHT_LINEAR+FX_AERIAL+FX_WIND"
    "high|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_RIM+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP+FX_LIGHT_LINEAR+FX_AERIAL+FX_WIND"
    "basic|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP"
    "basic_flat|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8"
    "basic_noshadow|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP"
    "basic_hardshadow|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP"
    "basic_rim|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_RIM+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP"
    "basic_linear|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP+FX_LIGHT_LINEAR"
    "basic_aerial|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP+FX_AERIAL"
    "basic_wind|FX_LIGHT_PER_PIXEL+FX_LIGHT_HEMISPHERE+FX_LIGHT_SPECULAR+FX_LIGHT_TRANSLUCENT+FX_LIGHT_SHADOW+FX_LIGHT_SHADOW_SOFT+FX_LIGHT_POINT+FX_LIGHT_POINT_MAX=8+FX_LIGHT_NORMALMAP+FX_WIND")
set(preview_table "")
foreach(variant ${preview_variants})
    string(REGEX MATCH "^([a-z_]+)\\|(.*)$" _ "${variant}")
    set(name "${CMAKE_MATCH_1}")
    string(REPLACE "+" ";" fields "${CMAKE_MATCH_2}")
    storm_fx_add_shader(preview_shaders vs_preview_mesh_${name} "${preview_sc}/vs_preview_mesh.sc" DEFINES ${fields})
    storm_fx_add_shader(preview_shaders fs_preview_mesh_${name} "${preview_sc}/fs_preview_mesh.sc" DEFINES ${fields})
    list(JOIN fields "$<SEMICOLON>" joined)
    string(APPEND preview_table "PREVIEW_VARIANT(\"${joined}\", vs_preview_mesh_${name}, fs_preview_mesh_${name})\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen/preview_shaders/variants.inc" CONTENT "${preview_table}")
storm_fx_build_shaders(TARGET preview_shaders NAMESPACE preview_shaders VARYING "${preview_sc}/varying.def.sc"
    INCLUDES "${STORM_FX_DIR}/shaders/include" SHADERS ${preview_shaders})

add_executable(fx_preview main.cpp scene.cpp scene.hpp)
target_compile_features(fx_preview PRIVATE cxx_std_20)
target_link_libraries(fx_preview PRIVATE storm_bgfx_fx preview_shaders bgfx bx bimg bimg_encode)
add_custom_target(fx_shader_profiles DEPENDS storm_fx_shaders_all_profiles preview_shaders_all_profiles)
