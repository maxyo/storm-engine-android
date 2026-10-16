# this is something I really don't like but it seems there's not better way to ensure mimalloc is linked before c-runtime
#include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
#if (WIN32)
#  set(MIMALLOC_DEP "$<IF:$<CONFIG:Release>,${CONAN_LIB_DIRS_MIMALLOC_RELEASE}/mimalloc.lib,${CONAN_LIB_DIRS_MIMALLOC_DEBUG}/mimalloc-debug.lib>")
#else()
#  set(MIMALLOC_DEP "$<IF:$<CONFIG:Release>,${CONAN_LIB_DIRS_MIMALLOC_RELEASE}/libmimalloc.so,${CONAN_LIB_DIRS_MIMALLOC_DEBUG}/libmimalloc-debug.so>")
#endif()

if (WIN32)
set(SYSTEM_DEPS
    "comctl32"
    "dbghelp"
    "winhttp"
)
elseif(ANDROID)
set(SYSTEM_DEPS log android)
else()
set(SYSTEM_DEPS "ffi")
endif()

STORM_SETUP(
    TARGET_NAME engine
    TYPE executable
    DEPENDENCIES

    ${MIMALLOC_DEP}

    # internal
    core
    diagnostics
    util
    window

    # external
    mimalloc
    $<$<BOOL:${STORM_ENABLE_CRASH_REPORTS}>:sentry-native>
    ${SDL2_LIBRARIES}
    zlib

    # system
    ${SYSTEM_DEPS}
)

if(ANDROID)
    set_target_properties(engine PROPERTIES VERSION "" SOVERSION "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
endif()

if(STORM_ENABLE_BGFX)
    target_link_libraries(engine PRIVATE storm_bgfx_preview)
    target_compile_definitions(engine PRIVATE STORM_ENABLE_BGFX=1)
endif()
