# The single bgfx pin, shared by the engine build and host-tools/ (host shaderc).
include(FetchContent)
# bgfx.cmake pins bgfx/bx/bimg as gitlinks (see README.md). Never track master.
# Local fixes on top of the pin: patches/bgfx-*.patch, applied to the bgfx submodule.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=/path/to/pinned, patched checkout
file(GLOB storm_bgfx_patches "${CMAKE_CURRENT_LIST_DIR}/../patches/bgfx-*.patch")
storm_fetch_declare(storm_bgfx_upstream
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG 8d950d6dc3df1d763e7d3655987c89009767a07e
    GIT_SUBMODULES bgfx bx bimg
    GIT_SUBMODULES_RECURSE TRUE
    STORM_PATCHES bgfx ${storm_bgfx_patches})
