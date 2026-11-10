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


if(NOT ANDROID AND NOT WIN32)
    # The engine's techniques next to the binary, laid over the game's at run time
    # (FILE_SERVICE::ScanResourcePaths), in the layout of the APK's engine-shaders layer.
    file(GLOB_RECURSE STORM_TECHNIQUES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/techniques/*")
    set(STORM_ENGINE_SHADERS "$<TARGET_FILE_DIR:engine>/engine-shaders")
    add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${STORM_ENGINE_SHADERS}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/techniques" "${STORM_ENGINE_SHADERS}/RESOURCE/techniques"
        VERBATIM)
    set_property(TARGET engine APPEND PROPERTY LINK_DEPENDS ${STORM_TECHNIQUES})
endif()
