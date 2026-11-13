# Adds the pinned, patched bgfx/bx/bimg targets (bgfx_pin.cmake) once per build.
# Used by this directory (renderer_bgfx/CMakeLists.txt, which defines storm_bgfx_runtime).
# Callers set BGFX_BUILD_TOOLS and the bgfx limits before including.
# EXCLUDE_FROM_ALL: only targets that something links get built.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/bgfx_pin.cmake")
storm_bgfx_add()
foreach(target bgfx bx bimg bimg_decode bimg_encode)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()
# The ETC2/EAC encoder (etcpak in bimg_encode) runs on the player's device for the texture transcode and the
# relief maps: -O3 in every build type, and its SSE4.1 paths on x86-64 (part of the x86-64 Android ABI and of every
# development host; arm64 takes its NEON paths by default).
target_compile_options(bimg_encode PRIVATE -O3)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(bimg_encode PRIVATE -msse4.1)
endif()
