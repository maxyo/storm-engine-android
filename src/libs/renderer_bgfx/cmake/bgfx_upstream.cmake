# Adds the pinned bgfx/bx/bimg targets (bgfx_pin.cmake + the local patches/) once per build.
# Used by this directory and by projects that only need bimg (the standalone UI runtime and
# image decoding checks). Callers set BGFX_BUILD_TOOLS and the bgfx limits before including.
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
FetchContent_GetProperties(storm_bgfx_upstream)
if(NOT storm_bgfx_upstream_POPULATED)
    FetchContent_Populate(storm_bgfx_upstream)
    # Local fixes on top of the pinned bgfx (see patches/). Applied once; a tree that already
    # carries a patch is left alone, anything else is a hard error.
    file(GLOB bgfx_patches "${CMAKE_CURRENT_LIST_DIR}/../patches/bgfx-*.patch")
    foreach(patch ${bgfx_patches})
        execute_process(COMMAND git apply --reverse --check "${patch}"
            WORKING_DIRECTORY "${storm_bgfx_upstream_SOURCE_DIR}/bgfx" RESULT_VARIABLE already_applied
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT already_applied EQUAL 0)
            execute_process(COMMAND git apply "${patch}"
                WORKING_DIRECTORY "${storm_bgfx_upstream_SOURCE_DIR}/bgfx" RESULT_VARIABLE patch_result)
            if(NOT patch_result EQUAL 0)
                message(FATAL_ERROR "Cannot apply ${patch} to the pinned bgfx")
            endif()
        endif()
    endforeach()
    add_subdirectory("${storm_bgfx_upstream_SOURCE_DIR}" "${storm_bgfx_upstream_BINARY_DIR}" EXCLUDE_FROM_ALL)
    foreach(target bgfx bx bimg bimg_decode)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
            target_compile_options(${target} PRIVATE -w)
        endif()
    endforeach()
endif()
set(STORM_BGFX_SOURCE_DIR "${storm_bgfx_upstream_SOURCE_DIR}" CACHE INTERNAL "")
