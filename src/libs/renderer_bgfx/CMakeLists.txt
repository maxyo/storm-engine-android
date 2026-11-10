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
if(CMAKE_CROSSCOMPILING)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
endif()
# bgfx.cmake pins bgfx/bx/bimg as gitlinks (see README.md). Never track master.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=/path/to/pinned/checkout
FetchContent_Declare(storm_bgfx_upstream
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG 8d950d6dc3df1d763e7d3655987c89009767a07e
    GIT_SUBMODULES bgfx bx bimg
    GIT_SUBMODULES_RECURSE TRUE)
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

# Host shader compiler. Cross builds must supply one built for the host:
#   cmake -S src/libs/renderer_bgfx/tools-host -B build/bgfx-host-tools && cmake --build ... --target shaderc
if(CMAKE_CROSSCOMPILING)
    set(STORM_SHADERC "" CACHE FILEPATH "Host bgfx shaderc built from the pinned sources")
    if(NOT EXISTS "${STORM_SHADERC}")
        message(FATAL_ERROR "Set STORM_SHADERC to a host shaderc built from the pinned bgfx (see src/libs/renderer_bgfx/README.md)")
    endif()
    set(STORM_SHADERC_COMMAND "${STORM_SHADERC}" CACHE INTERNAL "")
    set(STORM_SHADERC_DEPENDS "${STORM_SHADERC}" CACHE INTERNAL "")
else()
    set(STORM_SHADERC_COMMAND "$<TARGET_FILE:shaderc>" CACHE INTERNAL "")
    set(STORM_SHADERC_DEPENDS shaderc CACHE INTERNAL "")
endif()
set(STORM_BGFX_SHADER_PROFILE "300_es" CACHE STRING "shaderc profile for embedded shader blobs")
set(STORM_BGFX_SHADER_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include" CACHE INTERNAL "")
include(cmake/storm_shaders.cmake)

file(GLOB shader_sources CONFIGURE_DEPENDS shaders/*.sc)
list(FILTER shader_sources EXCLUDE REGEX "varying\\.def\\.sc$")
storm_bgfx_embed_shaders(TARGET storm_bgfx_shaders NAMESPACE storm_shaders
    VARYING "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc" SOURCES ${shader_sources})

file(GLOB_RECURSE shim_sources CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
add_library(storm_bgfx STATIC ${shim_sources})
target_compile_features(storm_bgfx PUBLIC cxx_std_20)
target_include_directories(storm_bgfx PUBLIC include "${CMAKE_SOURCE_DIR}/third_party/d3d9-headers/directx"
    "${CMAKE_SOURCE_DIR}/third_party/d3d9-headers/windows" PRIVATE src)
target_link_libraries(storm_bgfx PUBLIC bgfx bx bimg PRIVATE storm_bgfx_shaders ${SDL2_LIBRARIES})
set_target_properties(storm_bgfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(storm_bgfx PRIVATE -fsigned-char)
target_compile_definitions(storm_bgfx PRIVATE NOMINMAX)
