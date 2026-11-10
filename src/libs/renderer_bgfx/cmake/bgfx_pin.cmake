# The single bgfx pin, shared by the engine build and host-tools/ (host shaderc).
include(FetchContent)
# bgfx.cmake pins bgfx/bx/bimg as gitlinks (see README.md). Never track master.
# Local fixes on top of the pin: patches/bgfx-*.patch, applied to the bgfx submodule.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=/path/to/pinned, patched checkout
# CONFIGURE_DEPENDS + the patches as configure dependencies: adding, removing or editing a patch
# reconfigures every existing build directory on its next build, which moves it to the source copy of
# the new patch set (storm_fetch_declare keys the shared copy by the patch contents).
file(GLOB storm_bgfx_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../patches/bgfx-*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${storm_bgfx_patches})
storm_fetch_declare(storm_bgfx_upstream
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG 8d950d6dc3df1d763e7d3655987c89009767a07e
    GIT_SUBMODULES bgfx bx bimg
    GIT_SUBMODULES_RECURSE TRUE
    STORM_PATCHES bgfx ${storm_bgfx_patches})
# bgfx.cmake keeps the bgfx/bx/bimg locations as cache entries from the first configure; they must
# follow the source copy of the current pin and patch set, or a warm build directory keeps compiling
# the previous one.
function(storm_bgfx_point_at source)
    foreach(part BGFX BX BIMG)
        string(TOLOWER "${part}" dir)
        if(DEFINED CACHE{${part}_DIR} AND NOT "$CACHE{${part}_DIR}" STREQUAL "${source}/${dir}")
            message(STATUS "bgfx pin / patch set changed: ${part}_DIR moves from $CACHE{${part}_DIR} to ${source}/${dir}")
        endif()
        set(${part}_DIR "${source}/${dir}" CACHE STRING "Location of ${dir} (the pinned, patched copy)" FORCE)
    endforeach()
endfunction()
