# bgfx is the engine's only GPU backend. This directory owns the pinned
# bgfx/bx/bimg dependency, the offline shader build and the D3D9-shaped shim
# (see README.md). Other libraries link `storm_bgfx` and never fetch bgfx.
cmake_minimum_required(VERSION 3.21)
include(FetchContent)
# Third-party code must not inherit the engine's global flags (UNICODE, the
# Windows type headers, -std=c++20 for C sources ...).
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "" COMPILE_DEFINITIONS "" COMPILE_OPTIONS "")
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
# The engine renders from its main thread; bgfx::frame() executes the backend
# inline, so the EGL context lives on the thread that owns the SDL window.
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_MAX_TEXTURES 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_VERTEX_LAYOUTS 256 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_DYNAMIC_INDEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_VERTEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_INDEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_FRAME_BUFFERS 256 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_TRANSIENT_VERTEX_BUFFER_SIZE 33554432 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_TRANSIENT_INDEX_BUFFER_SIZE 8388608 CACHE STRING "" FORCE)
# Shaders compile with a host shaderc built once per machine and pin (cmake/storm_shaderc.cmake),
# never with bgfx's tools inside this build.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/bgfx_pin.cmake")
FetchContent_GetProperties(storm_bgfx_upstream)
if(NOT storm_bgfx_upstream_POPULATED)
    FetchContent_Populate(storm_bgfx_upstream)
    # Upstream warning flags and tools must not inherit the engine's global
    # definitions (UNICODE etc.); EXCLUDE_FROM_ALL keeps unused tools unbuilt.
    add_subdirectory("${storm_bgfx_upstream_SOURCE_DIR}" "${storm_bgfx_upstream_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()
set(STORM_BGFX_SOURCE_DIR "${storm_bgfx_upstream_SOURCE_DIR}" CACHE INTERNAL "")
# Android and the Linux development host both run the GLES 3.0 backend so the
# same ESSL shader blobs and code paths are exercised. Selecting one renderer
# explicitly disables all the others (bgfx/src/config.h).
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGLES=30)
if(ANDROID)
    target_link_libraries(bgfx PUBLIC android log EGL GLESv3)
else()
    target_link_libraries(bgfx PUBLIC EGL GLESv2)
endif()
foreach(target bgfx bx bimg bimg_decode)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()

# etcpak: ETC2 encoder for the DXT -> ETC2 texture transcode (src/texture).
# Only the block compressors are built; no application, image I/O or threading code.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_ETCPAK=/path/to/pinned/checkout
storm_fetch_declare(storm_etcpak
    GIT_REPOSITORY https://github.com/wolfpld/etcpak
    GIT_TAG 3d716e1550023dc5ff8b602636af8b76584e0b66)
FetchContent_GetProperties(storm_etcpak)
if(NOT storm_etcpak_POPULATED)
    FetchContent_Populate(storm_etcpak)
endif()
add_library(storm_etcpak STATIC
    "${storm_etcpak_SOURCE_DIR}/ProcessRGB.cpp"
    "${storm_etcpak_SOURCE_DIR}/Tables.cpp"
    "${storm_etcpak_SOURCE_DIR}/Dither.cpp")
target_include_directories(storm_etcpak SYSTEM INTERFACE "${storm_etcpak_SOURCE_DIR}")
target_compile_features(storm_etcpak PRIVATE cxx_std_17)
target_compile_options(storm_etcpak PRIVATE -w -O3)
target_compile_definitions(storm_etcpak PRIVATE NDEBUG)
# SSE4.1 is part of the x86-64 Android ABI and of every x86-64 development host;
# arm64 uses etcpak's NEON paths by default.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(storm_etcpak PRIVATE -msse4.1)
endif()
set_target_properties(storm_etcpak PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(cmake/storm_shaderc.cmake)
set(STORM_BGFX_SHADER_PROFILE "300_es" CACHE STRING "shaderc profile for embedded shader blobs")
set(STORM_BGFX_SHADER_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include" CACHE INTERNAL "")
include(cmake/storm_shaders.cmake)

file(GLOB shader_sources CONFIGURE_DEPENDS shaders/*.sc shaders/ports/*.sc)
list(FILTER shader_sources EXCLUDE REGEX "varying\\.def\\.sc$")
storm_bgfx_embed_shaders(TARGET storm_bgfx_shaders NAMESPACE storm_shaders
    VARYING "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc" SOURCES ${shader_sources})

file(GLOB_RECURSE shim_sources CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
list(FILTER shim_sources EXCLUDE REGEX "/src/texture/core/")
add_library(storm_bgfx STATIC ${shim_sources})
target_compile_features(storm_bgfx PUBLIC cxx_std_20)
target_include_directories(storm_bgfx PUBLIC include "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/d3d9-headers/directx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/d3d9-headers/windows" PRIVATE src)
target_link_libraries(storm_bgfx PUBLIC bgfx bx bimg PRIVATE storm_bgfx_shaders ${SDL2_LIBRARIES})
set_target_properties(storm_bgfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(storm_bgfx PRIVATE -fsigned-char)
target_compile_definitions(storm_bgfx PRIVATE NOMINMAX)

# DXT -> ETC2 transcode and on-disk cache (no bgfx dependency; unit-tested on the host).
file(GLOB texture_core_sources CONFIGURE_DEPENDS src/texture/core/*.cpp src/texture/core/*.hpp)
add_library(storm_texture_core STATIC ${texture_core_sources})
target_compile_features(storm_texture_core PUBLIC cxx_std_20)
target_include_directories(storm_texture_core PUBLIC src/texture/core)
target_link_libraries(storm_texture_core PRIVATE storm_etcpak)
find_package(Threads REQUIRED)
target_link_libraries(storm_texture_core PUBLIC Threads::Threads)
set_target_properties(storm_texture_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(storm_bgfx PRIVATE storm_texture_core)

# Texture transcode tests and benchmark: only on request, so projects that merely consume
# storm_bgfx (e.g. the standalone UI build) do not build them.
option(STORM_BGFX_BUILD_TESTS "Build the renderer_bgfx texture tests and benchmark" OFF)
if(STORM_BGFX_BUILD_TESTS)
    add_executable(storm_texture_bench tests/texture/bench.cpp)
    target_link_libraries(storm_texture_bench PRIVATE storm_texture_core)
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(storm_texture_tests tests/texture/tests.cpp)
        target_link_libraries(storm_texture_tests PRIVATE storm_texture_core bimg_decode bimg bx)
        enable_testing()
        add_test(NAME storm_texture_tests COMMAND storm_texture_tests)
    endif()
endif()
