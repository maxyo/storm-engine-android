if(STORM_ENABLE_CRASH_REPORTS)
	add_definitions(-DSTORM_ENABLE_CRASH_REPORTS=1)
endif()

# watermark.hpp includes the generated storm_build_watermark.hpp by name.
get_filename_component(storm_watermark_dir "${STORM_WATERMARK_FILE}" DIRECTORY)
include_directories("${storm_watermark_dir}")

if(STORM_ENABLE_SAFE_MODE)
  add_definitions(-DSTORM_ENABLE_SAFE_MODE=1)
endif()

option(STORM_SCENE_SWEEP "Build the script hook used by tools/scene_sweep (never for shipped builds)" OFF)
if(STORM_SCENE_SWEEP)
  add_definitions(-DSTORM_SCENE_SWEEP=1)
endif()

add_compile_options(-std=c++20)
# Matrices, vertex data and D3D structures are viewed through differently
# typed pointers throughout the engine (storm_glm.h accessors, vertex locks).
add_compile_options(-fno-strict-aliasing)

if(ANDROID)
    # Same optimization level as the RelWithDebInfo builds that were tested on
    # devices; cmake/android.cmake adds LTO and hidden visibility for Release.
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2 -g -DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
endif()
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g")

# Add _DEBUG flag
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")

# Tracy profiler in development builds (-DSTORM_TRACY=ON); before the libraries, which bgfx's profiler follows.
include(tracy)

# this is crucial to include libs first
add_subdirectory(libs)
add_subdirectory(apps)

# The sources that record Tracy zones (src/libs/util/include/profiler.hpp); nothing changes without STORM_TRACY.
storm_tracy_sources(engine "${CMAKE_CURRENT_SOURCE_DIR}/apps/engine/src/main.cpp")
storm_tracy_sources(core "${CMAKE_CURRENT_SOURCE_DIR}/libs/core/src/core_impl.cpp")
storm_tracy_sources(storm_bgfx "${CMAKE_CURRENT_SOURCE_DIR}/libs/renderer_bgfx/src/shim/context.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/renderer_bgfx/src/shim/program_cache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/renderer_bgfx/src/shim/frame_pacing.cpp")
