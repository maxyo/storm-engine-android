# Host check of the bgfx renderer: builds storm_bgfx (shim, shaders compiled by the pinned shaderc)
# runs the texture transcode tests and the draw/state counter check (perf_check.cpp).
# Configured by run.py next to this file.
cmake_minimum_required(VERSION 3.21)
project(storm_renderer_bgfx_check LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
set(CMAKE_CXX_STANDARD 20)
set(STORM_SDL_INCLUDE "" CACHE PATH "directory that contains SDL2/SDL.h")
add_library(storm_check_sdl INTERFACE)
target_include_directories(storm_check_sdl INTERFACE "${STORM_SDL_INCLUDE}")
set(SDL2_LIBRARIES storm_check_sdl)
set(STORM_BGFX_BUILD_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx" renderer_bgfx)
# The relief preparation (onnxruntime + DeepBump): storm_relief_check.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/relief" relief)
# The pinned SDL source headers carry no platform configuration; the checks run on the host's X11
# SDL runtime, so the shim takes its native window from SDL's X11 window info (skinning_check).
target_compile_definitions(storm_bgfx PRIVATE SDL_VIDEO_DRIVER_X11=1)

# perf_check drives the shim on bgfx's Noop backend; the shim calls a few SDL window functions,
# so it links the host's SDL2 runtime like the other SDL host checks.
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)
add_executable(perf_check perf_check.cpp)
target_include_directories(perf_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(perf_check PRIVATE NOMINMAX)
target_compile_options(perf_check PRIVATE -fsigned-char)
target_link_libraries(perf_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")

# skinning_check renders through OpenGL ES in an SDL window (an X display: xvfb-run in CI) and
# compares GPU skinning with the CPU kernel of src/libs/model.
add_executable(skinning_check skinning_check.cpp)
target_include_directories(skinning_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/model/src" "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/model/include")
target_compile_definitions(skinning_check PRIVATE NOMINMAX)
target_compile_options(skinning_check PRIVATE -fsigned-char)
target_link_libraries(skinning_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")

# preset_switch_check: a live graphics preset switch through the shim on OpenGL ES (an X display, like
# skinning_check): the previous settings draw until the new ones are prepared, no black frame after it.
add_executable(preset_switch_check preset_switch_check.cpp)
target_include_directories(preset_switch_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(preset_switch_check PRIVATE NOMINMAX)
target_compile_options(preset_switch_check PRIVATE -fsigned-char)
target_link_libraries(preset_switch_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")

# program_cache_check: program binaries between renderer starts (src/shim/program_cache.hpp) on OpenGL ES (an X
# display, like skinning_check): stored, loaded, corrupt / rejected entries relinked, the size limit.
add_executable(program_cache_check program_cache_check.cpp)
target_include_directories(program_cache_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(program_cache_check PRIVATE NOMINMAX)
target_compile_options(program_cache_check PRIVATE -fsigned-char)
target_link_libraries(program_cache_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")

# ao_volume_check: the interior AO volume end to end on OpenGL ES (an X display, like skinning_check): baked floor
# under a table top, «Низкая» untouched, the baked variant without the volume unchanged, levels 1..3 darker under it.
add_executable(ao_volume_check ao_volume_check.cpp)
target_include_directories(ao_volume_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(ao_volume_check PRIVATE NOMINMAX)
target_compile_options(ao_volume_check PRIVATE -fsigned-char)
target_link_libraries(ao_volume_check PRIVATE storm_bgfx "${STORM_SDL_RUNTIME}")
