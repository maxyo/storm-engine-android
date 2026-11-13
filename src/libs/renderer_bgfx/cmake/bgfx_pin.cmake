# The single bgfx pin, shared by the engine build (bgfx_upstream.cmake) and host-tools/ (host shaderc).
# bgfx.cmake supplies only the CMake scripts; bgfx, bx and bimg are pinned here directly (bgfx.cmake itself
# fetches them with FetchContent and takes BGFX_DIR / BX_DIR / BIMG_DIR when they are set). Never track master.
# Local fixes on top of the pin: patches/bgfx-*.patch, applied in the bgfx tree.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_BGFX=<pinned, patched bgfx checkout> and
# -DFETCHCONTENT_SOURCE_DIR_STORM_{BGFX_CMAKE,BX,BIMG}=<pinned checkouts>.
# CONFIGURE_DEPENDS + the patches as configure dependencies: adding, removing or editing a patch
# reconfigures every existing build directory on its next build, which updates only the changed files of
# its patched view (storm_patched_view in cmake/StormBuildCache.cmake); its path stays the same.
include(FetchContent)
file(GLOB storm_bgfx_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../patches/bgfx-*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${storm_bgfx_patches})
# bgfx.cmake be0ef0d (2026-11-10).
storm_fetch_declare(storm_bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG be0ef0d69a9ce609de99dd420f1182c0888cc8f1
    STORM_POPULATE_ONLY)
# bgfx eebf5686 (2026-11-11, BGFX_API_VERSION 166).
storm_fetch_declare(storm_bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.git
    GIT_TAG eebf5686a8c5f1c77e1af2dad17b5746ba17da8f
    STORM_POPULATE_ONLY
    STORM_PATCHES . ${storm_bgfx_patches})
storm_fetch_declare(storm_bx
    GIT_REPOSITORY https://github.com/bkaradzic/bx.git
    GIT_TAG 57548da89a07771c5b0f8a6711149be70e08a329
    STORM_POPULATE_ONLY)
storm_fetch_declare(storm_bimg
    GIT_REPOSITORY https://github.com/bkaradzic/bimg.git
    GIT_TAG 22177183239e18d7af603e6915156371cc38d18d
    STORM_POPULATE_ONLY)

# Populates the pinned trees and adds the bgfx.cmake targets once per build (EXCLUDE_FROM_ALL: only targets
# that something links get built). Sets STORM_BGFX_SOURCE_DIR to the patched bgfx tree.
macro(storm_bgfx_add)
    FetchContent_GetProperties(storm_bgfx_cmake)
    if(NOT storm_bgfx_cmake_POPULATED)
        FetchContent_MakeAvailable(storm_bgfx_cmake storm_bgfx storm_bx storm_bimg)
        # bgfx.cmake keeps BGFX_DIR / BX_DIR / BIMG_DIR in the cache; they must follow the current pin, or a
        # warm build directory keeps compiling the previous trees.
        foreach(storm_part BGFX BX BIMG)
            string(TOLOWER "${storm_part}" storm_dir)
            set(storm_source "${storm_${storm_dir}_SOURCE_DIR}")
            if(DEFINED CACHE{${storm_part}_DIR} AND NOT "$CACHE{${storm_part}_DIR}" STREQUAL "${storm_source}")
                message(STATUS "bgfx source tree changed: ${storm_part}_DIR moves from $CACHE{${storm_part}_DIR} to ${storm_source}")
            endif()
            set(${storm_part}_DIR "${storm_source}" CACHE STRING "Location of ${storm_dir} (the pinned copy)" FORCE)
        endforeach()
        add_subdirectory("${storm_bgfx_cmake_SOURCE_DIR}" "${storm_bgfx_cmake_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
    set(STORM_BGFX_SOURCE_DIR "${storm_bgfx_SOURCE_DIR}" CACHE INTERNAL "")
endmacro()
