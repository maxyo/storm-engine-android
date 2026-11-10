# Host tool: compiles a game's whole script tree (no renderer, no game start) and reports every
# compile error. See src/apps/script_check/README.md.
if(ANDROID)
    return()
endif()

STORM_SETUP(
    TARGET_NAME script_check
    TYPE executable
    DEPENDENCIES
    core
    diagnostics
    util
    window
    mimalloc
    ${SDL2_LIBRARIES}
    zlib
    ffi
)
target_include_directories(script_check PRIVATE "${CMAKE_SOURCE_DIR}/src/libs/core/src")
