# Host checks of the PBR package (src/libs/renderer_bgfx/pbr): CPU unit checks and the headless GLES render
# check of a Caribbean Legend model. Configured by run.py next to this file.
cmake_minimum_required(VERSION 3.21)
project(storm_pbr_check LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
set(CMAKE_CXX_STANDARD 20)
set(STORM_SDL_INCLUDE "" CACHE PATH "directory that contains SDL2/SDL.h")
add_library(storm_check_sdl INTERFACE)
target_include_directories(storm_check_sdl INTERFACE "${STORM_SDL_INCLUDE}")
set(SDL2_LIBRARIES storm_check_sdl)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx" renderer_bgfx)

# Render check: the production .gm loader (geometry) feeds a Caribbean Legend model to the PBR renderer.
set(STORM_FMT_SOURCE "" CACHE PATH "fmt source tree (header-only use), needed by the .gm loader")
if(STORM_FMT_SOURCE)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/glm.cmake")
    set(libs "${CMAKE_CURRENT_LIST_DIR}/../../src/libs")
    add_executable(pbr_render_check EXCLUDE_FROM_ALL render_check.cpp "${libs}/geometry/src/geom_static.cpp" "${libs}/geometry/src/collide.cpp")
    target_include_directories(pbr_render_check PRIVATE "${libs}/geometry/src" "${libs}/geometry/include" "${libs}/math/include"
        "${libs}/core/include" "${STORM_FMT_SOURCE}/include")
    target_compile_definitions(pbr_render_check PRIVATE FMT_HEADER_ONLY)
    target_link_libraries(pbr_render_check PRIVATE storm_bgfx_pbr storm_texture_formats glm::glm-header-only bimg_encode bimg_decode bimg bx)
endif()

# The shim's PBR hook on bgfx's Noop backend (like tests/renderer_bgfx/perf_check).
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)
add_executable(pbr_shim_check EXCLUDE_FROM_ALL shim_check.cpp)
target_include_directories(pbr_shim_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(pbr_shim_check PRIVATE NOMINMAX)
target_compile_options(pbr_shim_check PRIVATE -fsigned-char)
target_link_libraries(pbr_shim_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")
