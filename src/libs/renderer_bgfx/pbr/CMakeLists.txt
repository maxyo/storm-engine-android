# PBR materials and image-based lighting for Caribbean Legend models (see README.md).
#
# Added by renderer_bgfx (or tests/pbr) with add_subdirectory() after the bgfx targets and
# storm_bgfx_embed_shaders exist. Needs the scene_data target (linked by name).
#   storm_bgfx_pbr_core  CPU: material resolution, IBL bake (bimg), BRDF table, preset constants
#   storm_bgfx_pbr       GPU: programs, environment textures, per-draw binding (bgfx only)
cmake_minimum_required(VERSION 3.21)
if(NOT TARGET bimg_encode)
    message(FATAL_ERROR "renderer_bgfx/pbr needs the pinned bgfx targets (bimg_encode)")
endif()
set_target_properties(bimg_encode PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(bimg_encode PRIVATE -w)

add_library(storm_bgfx_pbr_core STATIC src/material.cpp src/environment.cpp)
target_compile_features(storm_bgfx_pbr_core PUBLIC cxx_std_20)
target_include_directories(storm_bgfx_pbr_core PUBLIC include)
target_link_libraries(storm_bgfx_pbr_core PUBLIC scene_data PRIVATE bimg_encode bimg_decode bimg bx)
set_target_properties(storm_bgfx_pbr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB pbr_shader_sources CONFIGURE_DEPENDS shaders/*.sc)
list(FILTER pbr_shader_sources EXCLUDE REGEX "varying\\.def\\.sc$")
storm_bgfx_embed_shaders(TARGET storm_pbr_shaders NAMESPACE storm_pbr_shaders
    VARYING "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc" SOURCES ${pbr_shader_sources}
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include")
add_library(storm_bgfx_pbr STATIC src/renderer.cpp)
target_link_libraries(storm_bgfx_pbr PUBLIC storm_bgfx_pbr_core bgfx bx PRIVATE storm_pbr_shaders)
set_target_properties(storm_bgfx_pbr PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(pbr_core_check EXCLUDE_FROM_ALL tests/core_check.cpp)
    target_link_libraries(pbr_core_check PRIVATE storm_bgfx_pbr_core)
endif()
