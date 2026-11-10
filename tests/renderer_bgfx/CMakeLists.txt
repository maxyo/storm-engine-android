# Host check of the bgfx renderer: builds storm_bgfx (shim, shaders compiled by the pinned shaderc)
# runs the texture transcode tests and the draw/state counter check (perf_check.cpp).
# Configured by run.py next to this file.
cmake_minimum_required(VERSION 3.21)
project(storm_renderer_bgfx_check LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(STORM_SDL_INCLUDE "" CACHE PATH "directory that contains SDL2/SDL.h")
add_library(storm_check_sdl INTERFACE)
target_include_directories(storm_check_sdl INTERFACE "${STORM_SDL_INCLUDE}")
set(SDL2_LIBRARIES storm_check_sdl)
set(STORM_BGFX_BUILD_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx" renderer_bgfx)

# perf_check drives the shim on bgfx's Noop backend; the shim calls a few SDL window functions,
# so it links the host's SDL2 runtime like the other SDL host checks.
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)
add_executable(perf_check perf_check.cpp)
target_include_directories(perf_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(perf_check PRIVATE NOMINMAX)
target_compile_options(perf_check PRIVATE -fsigned-char)
target_link_libraries(perf_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")
