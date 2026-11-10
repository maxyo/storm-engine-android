# Builds only the host bgfx shaderc from the engine's bgfx pin, for cross builds
# (pass the result as -DSTORM_SHADERC=<binary dir>/shaderc).
#   cmake -S src/libs/renderer_bgfx/host-tools -B build/bgfx-host-tools -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/bgfx-host-tools
cmake_minimum_required(VERSION 3.21)
project(storm_bgfx_host_tools LANGUAGES C CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/bgfx_pin.cmake")
FetchContent_GetProperties(storm_bgfx_upstream)
if(NOT storm_bgfx_upstream_POPULATED)
    FetchContent_Populate(storm_bgfx_upstream)
    add_subdirectory("${storm_bgfx_upstream_SOURCE_DIR}" "${storm_bgfx_upstream_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()
add_custom_target(storm_shaderc ALL DEPENDS shaderc)
