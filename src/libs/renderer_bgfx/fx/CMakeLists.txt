# Graphics effects package for the bgfx renderer (see README.md).
#
# This directory is self-contained and is NOT picked up by the engine build on
# its own: the renderer (or tools/fx-preview) adds it with add_subdirectory()
# after bgfx exists. Required from the parent scope:
#   target   storm_bgfx_runtime (bgfx, bx, bimg)
#   STORM_FX_SHADERC_COMMAND       host shaderc built from the pinned bgfx
#   STORM_FX_SHADERC_DEPENDS       target/file the shader build depends on
#   STORM_FX_BGFX_SHADER_INCLUDE   <bgfx>/src (bgfx_shader.sh)
# render/bgfx can pass its STORM_SHADERC_COMMAND / STORM_SHADERC_DEPENDS and
# "${STORM_BGFX_SOURCE_DIR}/bgfx/src" unchanged.
cmake_minimum_required(VERSION 3.21)
if(NOT TARGET storm_bgfx_runtime)
    message(FATAL_ERROR "renderer_bgfx/fx needs the pinned bgfx (storm_bgfx_runtime); configure tools/fx-preview or add it from renderer_bgfx")
endif()
set(STORM_FX_SHADER_PROFILE "300_es" CACHE STRING "shaderc profile for the embedded fx shader blobs")
include(cmake/storm_fx_shaders.cmake)
include(shaders/shaders.cmake)

# voxelizer.h (MIT, header-only): the triangle/box overlap test of the AO volume bake (src/voxelizer_impl.c).
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_VOXELIZER=/path/to/this/revision
include(FetchContent)
storm_fetch_declare(storm_voxelizer
    URL https://codeload.github.com/karimnaaji/voxelizer/tar.gz/f6ee9cf6863c034853a587f205f12183c7d57b74
    URL_HASH SHA256=8b6971f0c8c3e79ca0feb6ef8f93cbf23378672159ebd58d458ff1996385be45
    STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_voxelizer)

add_library(storm_bgfx_fx_core STATIC
    src/color_lut.cpp src/noise_bake.cpp src/sky_model.cpp
    src/lighting_model.cpp src/light_set.cpp src/shadow_model.cpp src/normal_map.cpp src/settings.cpp src/log.cpp
    src/ao_volume.cpp src/voxelizer_impl.c)
target_compile_features(storm_bgfx_fx_core PUBLIC cxx_std_20)
target_include_directories(storm_bgfx_fx_core PUBLIC include)
target_include_directories(storm_bgfx_fx_core SYSTEM PRIVATE "${storm_voxelizer_SOURCE_DIR}")
set_source_files_properties(src/voxelizer_impl.c PROPERTIES COMPILE_OPTIONS "-w")
find_package(Threads REQUIRED)
target_link_libraries(storm_bgfx_fx_core PUBLIC Threads::Threads) # the AO bake's workers
set_target_properties(storm_bgfx_fx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(storm_bgfx_fx STATIC
    src/gpu_util.cpp src/post_chain.cpp src/sea_depth.cpp src/sky.cpp src/lighting.cpp src/shadows.cpp src/capabilities.cpp
    src/contact_shadows.cpp src/self_test.cpp)
target_link_libraries(storm_bgfx_fx PUBLIC storm_bgfx_fx_core storm_bgfx_runtime PRIVATE storm_fx_shaders)
set_target_properties(storm_bgfx_fx PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(storm_fx_core_check tests/core_check.cpp)
    target_link_libraries(storm_fx_core_check PRIVATE storm_bgfx_fx_core)
    add_test(NAME fx_core COMMAND storm_fx_core_check)
endif()
