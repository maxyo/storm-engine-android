# Tracy profiler client (https://github.com/wolfpld/tracy, BSD-3-Clause:
# third_party/licenses/Tracy-BSD-3-Clause.txt) for development builds: -DSTORM_TRACY=ON. Off by default;
# release APKs never contain it (the application's build_apk.py refuses a libengine.so that has it).
# Connecting the viewer: tools/android/README.md, "Tracy profiler".
#
# Only the sources given to storm_tracy_sources() see Tracy (src/libs/util/include/profiler.hpp): every
# other compile command stays the same as without it, so a Tracy build directory recompiles just those
# files and bgfx (BGFX_CONFIG_PROFILER) next to the warm ccache entries of the normal build.
option(STORM_TRACY "Tracy profiler client in this build (development only)" OFF)
set(storm_tracy_version 0.14.1)
# On demand: nothing is recorded until a viewer connects. Only on the loopback interface (adb forward on
# a device, the viewer on the same machine on the host), no broadcast to the network, no crash handler
# (the engine's own handler and Android's exit info stay in charge), no systrace sampling or frame images.
set(storm_tracy_definitions TRACY_ENABLE TRACY_ON_DEMAND TRACY_ONLY_LOCALHOST TRACY_NO_BROADCAST
    TRACY_NO_CRASH_HANDLER TRACY_NO_SYSTEM_TRACING TRACY_NO_FRAME_IMAGE)
if(STORM_TRACY)
    storm_fetch_declare(storm_tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG 30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c # v0.14.1
        STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_tracy)
    find_package(Threads REQUIRED)
    add_library(storm_tracy STATIC "${storm_tracy_SOURCE_DIR}/public/TracyClient.cpp")
    target_compile_definitions(storm_tracy PRIVATE ${storm_tracy_definitions})
    target_compile_options(storm_tracy PRIVATE -w)
    set_target_properties(storm_tracy PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(storm_tracy PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    if(ANDROID)
        target_link_libraries(storm_tracy PUBLIC log)
    endif()
    message(STATUS "Tracy ${storm_tracy_version} profiler client: ON (development build)")
endif()

# storm_tracy_sources(<target> <absolute source paths>...): these sources of <target> record Tracy zones
# (STORM_TRACY and Tracy's configuration, its headers); <target> links the client. No-op when STORM_TRACY is off.
function(storm_tracy_sources target)
    if(NOT STORM_TRACY)
        return()
    endif()
    set_property(SOURCE ${ARGN} TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_DEFINITIONS
        STORM_TRACY ${storm_tracy_definitions})
    set_property(SOURCE ${ARGN} TARGET_DIRECTORY ${target} APPEND PROPERTY INCLUDE_DIRECTORIES
        "${storm_tracy_SOURCE_DIR}/public")
    target_link_libraries(${target} PRIVATE storm_tracy)
endfunction()
