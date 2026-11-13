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
# storm_check_sdl carries both: the pinned headers (the host has no SDL development package in CI)
# and the runtime, so every check that includes <SDL2/SDL.h> links it instead of the bare runtime.
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)
target_link_libraries(storm_check_sdl INTERFACE "${STORM_SDL_RUNTIME}")
add_executable(perf_check perf_check.cpp)
target_include_directories(perf_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(perf_check PRIVATE NOMINMAX)
target_compile_options(perf_check PRIVATE -fsigned-char)
target_link_libraries(perf_check PRIVATE storm_bgfx storm_check_sdl)

# backend_select_check: the choice between Vulkan and OpenGL ES and the marker of a failed Vulkan start
# (src/shim/backend_select.hpp). No GPU.
add_executable(backend_select_check backend_select_check.cpp)
target_include_directories(backend_select_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_link_libraries(backend_select_check PRIVATE storm_bgfx storm_check_sdl)

# skinning_check renders through OpenGL ES in an SDL window (an X display: xvfb-run in CI) and
# compares GPU skinning with the CPU kernel of src/libs/model.
add_executable(skinning_check skinning_check.cpp)
target_include_directories(skinning_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/model/src" "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/model/include")
target_compile_definitions(skinning_check PRIVATE NOMINMAX)
target_compile_options(skinning_check PRIVATE -fsigned-char)
target_link_libraries(skinning_check PRIVATE storm_bgfx storm_check_sdl)

# uniform_commit_check: the GL backend uploads only the uniforms a program does not hold yet
# (patches/bgfx-gl-uniform-commit.patch), on OpenGL ES (an X display, like skinning_check).
add_executable(uniform_commit_check uniform_commit_check.cpp)
target_include_directories(uniform_commit_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(uniform_commit_check PRIVATE NOMINMAX)
target_compile_options(uniform_commit_check PRIVATE -fsigned-char)
target_link_libraries(uniform_commit_check PRIVATE storm_bgfx storm_check_sdl)

# preset_switch_check: a live graphics preset switch through the shim on OpenGL ES (an X display, like
# skinning_check): the previous settings draw until the new ones are prepared, no black frame after it.
add_executable(preset_switch_check preset_switch_check.cpp)
target_include_directories(preset_switch_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(preset_switch_check PRIVATE NOMINMAX)
target_compile_options(preset_switch_check PRIVATE -fsigned-char)
target_link_libraries(preset_switch_check PRIVATE storm_bgfx storm_check_sdl)

# render_thread_check: bgfx's render thread through the shim on OpenGL ES (an X display, like skinning_check): the
# driver's strings arrive from it, the surface is released only with no frame in flight, both gate choices and
# render_thread=0 present, the renderer restarts in either mode.
add_executable(render_thread_check render_thread_check.cpp)
target_include_directories(render_thread_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(render_thread_check PRIVATE NOMINMAX)
target_compile_options(render_thread_check PRIVATE -fsigned-char)
target_link_libraries(render_thread_check PRIVATE storm_bgfx storm_check_sdl)

# program_cache_check: program binaries between renderer starts (src/shim/program_cache.hpp) on OpenGL ES (an X
# display, like skinning_check): stored, loaded, corrupt / rejected entries relinked, the size limit.
add_executable(program_cache_check program_cache_check.cpp)
target_include_directories(program_cache_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(program_cache_check PRIVATE NOMINMAX)
target_compile_options(program_cache_check PRIVATE -fsigned-char)
target_link_libraries(program_cache_check PRIVATE storm_bgfx storm_check_sdl)

# ao_volume_check: the interior AO volume end to end on OpenGL ES (an X display, like skinning_check): baked floor
# under a table top, «Низкая» untouched, the baked variant without the volume unchanged, levels 1..3 darker under it.
add_executable(ao_volume_check ao_volume_check.cpp)
target_include_directories(ao_volume_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(ao_volume_check PRIVATE NOMINMAX)
target_compile_options(ao_volume_check PRIVATE -fsigned-char)
target_link_libraries(ao_volume_check PRIVATE storm_bgfx storm_check_sdl)

# cutout_caster_check: a material whose texture alpha cuts holes casts with them in the lamp atlases (still geometry,
# characters) and the sun cascade; blended materials without depth write cast nothing (src/effects/casters.hpp).
add_executable(cutout_caster_check cutout_caster_check.cpp)
target_include_directories(cutout_caster_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(cutout_caster_check PRIVATE NOMINMAX)
target_compile_options(cutout_caster_check PRIVATE -fsigned-char)
target_link_libraries(cutout_caster_check PRIVATE storm_bgfx storm_check_sdl)

# sea_edge_check: the sea's soft edges (fs_port_sea2 + fx::SeaDepth, «Высокая») next to an object in front of the sea,
# through the shim on OpenGL ES (an X display, like skinning_check): no water is faded where nothing lies under it.
add_executable(sea_edge_check sea_edge_check.cpp)
target_include_directories(sea_edge_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(sea_edge_check PRIVATE NOMINMAX)
target_compile_options(sea_edge_check PRIVATE -fsigned-char)
target_link_libraries(sea_edge_check PRIVATE storm_bgfx storm_check_sdl)

# relief_switch_check: textures loaded while relief is off are looked up when it is switched on (a preset raised in a
# location): requested from the generator, height maps derived. No GPU (shim stores filled like TextureLoad).
add_executable(relief_switch_check relief_switch_check.cpp)
target_include_directories(relief_switch_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(relief_switch_check PRIVATE NOMINMAX)
target_compile_options(relief_switch_check PRIVATE -fsigned-char)
target_link_libraries(relief_switch_check PRIVATE storm_bgfx storm_check_sdl)

add_executable(texture_format_check texture_format_check.cpp)
target_include_directories(texture_format_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src")
target_compile_definitions(texture_format_check PRIVATE NOMINMAX)
target_compile_options(texture_format_check PRIVATE -fsigned-char)
target_link_libraries(texture_format_check PRIVATE storm_bgfx storm_check_sdl)

# buffer_pool_check: write-once buffers in bgfx's shared dynamic buffers (src/shim/draw.cpp) on OpenGL ES (an X display, like
# skinning_check): every range draws exactly its buffer's data, after releases, rewrites and another stride.
add_executable(buffer_pool_check buffer_pool_check.cpp)
target_include_directories(buffer_pool_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(buffer_pool_check PRIVATE NOMINMAX)
target_compile_options(buffer_pool_check PRIVATE -fsigned-char)
target_link_libraries(buffer_pool_check PRIVATE storm_bgfx storm_check_sdl)

# thumbnail_check: the save thumbnail of a background autosave (no surface): the back buffer is read from the copy of
# the last presented frame (src/shim/device.cpp copy_frame), on OpenGL ES (an X display, like skinning_check) and Vulkan.
add_executable(thumbnail_check thumbnail_check.cpp)
target_include_directories(thumbnail_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/renderer_bgfx/src/shim")
target_compile_definitions(thumbnail_check PRIVATE NOMINMAX)
target_compile_options(thumbnail_check PRIVATE -fsigned-char)
target_link_libraries(thumbnail_check PRIVATE storm_bgfx storm_check_sdl)
