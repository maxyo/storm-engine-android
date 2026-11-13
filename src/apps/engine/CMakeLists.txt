if(ANDROID)
set(SYSTEM_DEPS log android)
else()
set(SYSTEM_DEPS "ffi")
endif()

STORM_SETUP(
    TARGET_NAME engine
    TYPE executable
    DEPENDENCIES

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
    include(build_stamp)
    storm_build_stamp(engine)
    # The APK takes what the relief generation needs next to libengine.so (libonnxruntime.so, the PBRify model assets)
    # from the same <build>/game directory: building the engine puts them there (src/libs/relief/README.md).
    add_dependencies(engine storm_relief_package)
endif()

