# Included by every Storm native CMake project (engine, UI runtime, archive and torrent
# readers, host tools, tests). Compiles through tools/ccache/launcher (README.md there)
# and fails configuration otherwise. The only exception is an explicit, logged
#   -DSTORM_ALLOW_NO_CCACHE="<reason>"   (or the environment variable of that name)
# which compiles through tools/ccache/uncached, announcing every uncached compile.
# Also provides storm_fetch_declare(): FetchContent_Declare with one shared, read-only
# source copy per pin and machine (STORM_DEPS_CACHE, default ~/.cache/storm-deps).
include_guard(GLOBAL)
include(FetchContent)
get_filename_component(STORM_CCACHE_TOOLS "${CMAKE_CURRENT_LIST_DIR}/../tools/ccache" ABSOLUTE)

if(NOT DEFINED STORM_ALLOW_NO_CCACHE AND DEFINED ENV{STORM_ALLOW_NO_CCACHE})
    set(STORM_ALLOW_NO_CCACHE "$ENV{STORM_ALLOW_NO_CCACHE}")
endif()
if("${STORM_ALLOW_NO_CCACHE}" MATCHES "^(1|0|ON|OFF|TRUE|FALSE|YES|NO|Y|N)$")
    message(FATAL_ERROR "STORM_ALLOW_NO_CCACHE takes the reason as its value, e.g. "
        "STORM_ALLOW_NO_CCACHE=\"measuring a cold compile for <task>\"; got '${STORM_ALLOW_NO_CCACHE}'")
endif()
if(NOT "${STORM_ALLOW_NO_CCACHE}" STREQUAL "")
    set(storm_launcher "${STORM_CCACHE_TOOLS}/uncached;${STORM_ALLOW_NO_CCACHE}")
    message(WARNING "\n!!!!!!!! STORM ALARM: BUILDING WITHOUT CCACHE !!!!!!!!\n"
        "STORM_ALLOW_NO_CCACHE=\"${STORM_ALLOW_NO_CCACHE}\"\n"
        "Every compile of ${CMAKE_BINARY_DIR} is cold and announces itself. Never build cold "
        "except in exceptional cases (tools/ccache/README.md).\n")
else()
    find_program(STORM_CCACHE_PROGRAM ccache)
    if(NOT STORM_CCACHE_PROGRAM)
        message(FATAL_ERROR "STORM ALARM: ccache is not installed. Storm builds always compile "
            "through tools/ccache/launcher. Install ccache, or, in an exceptional case only, "
            "configure with STORM_ALLOW_NO_CCACHE=\"<reason>\" (tools/ccache/README.md).")
    endif()
    set(storm_launcher "${STORM_CCACHE_TOOLS}/launcher")
endif()
foreach(lang C CXX)
    # A plain `ccache` launcher (older instructions) would use another project's cache;
    # it is replaced. Any other launcher is a conflict.
    if(CMAKE_${lang}_COMPILER_LAUNCHER AND NOT CMAKE_${lang}_COMPILER_LAUNCHER STREQUAL storm_launcher
       AND NOT CMAKE_${lang}_COMPILER_LAUNCHER MATCHES "(^|/)ccache$"
       AND NOT CMAKE_${lang}_COMPILER_LAUNCHER MATCHES "/tools/ccache/(launcher|uncached)(;|$)")
        message(FATAL_ERROR "CMAKE_${lang}_COMPILER_LAUNCHER='${CMAKE_${lang}_COMPILER_LAUNCHER}': "
            "Storm builds compile through ${STORM_CCACHE_TOOLS}/launcher (tools/ccache/README.md).")
    endif()
    set(CMAKE_${lang}_COMPILER_LAUNCHER "${storm_launcher}" CACHE STRING
        "Set by cmake/StormBuildCache.cmake" FORCE)
endforeach()
unset(storm_launcher)

if(NOT STORM_DEPS_CACHE)
    if(DEFINED ENV{STORM_DEPS_CACHE})
        set(STORM_DEPS_CACHE "$ENV{STORM_DEPS_CACHE}")
    else()
        set(STORM_DEPS_CACHE "$ENV{HOME}/.cache/storm-deps")
    endif()
endif()
# storm_fetch_declare(<name> <FetchContent_Declare args> [STORM_PATCHES <dir> <patch>...])
# declares <name> with sources fetched once per machine into
# ${STORM_DEPS_CACHE}/fetchcontent/<name>-<digest of the arguments and patch contents>, shared
# by every build directory of every worktree; the patches are applied there with git apply in
# <dir> (relative to the source root) right after fetching. Builds never modify shared sources.
# An explicit FETCHCONTENT_SOURCE_DIR_<NAME> still wins.
function(storm_fetch_declare name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "STORM_PATCHES")
    set(fetch ${arg_UNPARSED_ARGUMENTS})
    string(TOUPPER "${name}" upper)
    # FetchContent keeps FETCHCONTENT_SOURCE_DIR_<NAME> in the cache. A value inside our own shared copies is the
    # one this function chose at an earlier configure, not an explicit override: it is recomputed, so a changed
    # pin or patch set moves a warm build directory to the matching copy.
    set(shared_prefix "${STORM_DEPS_CACHE}/fetchcontent/${name}-")
    string(FIND "${FETCHCONTENT_SOURCE_DIR_${upper}}" "${shared_prefix}" shared_at)
    if(NOT FETCHCONTENT_SOURCE_DIR_${upper} OR shared_at EQUAL 0)
        set(key "${fetch}")
        set(patches ${arg_STORM_PATCHES})
        if(patches)
            list(POP_FRONT patches patch_dir)
            string(APPEND key ";${patch_dir}")
            foreach(patch ${patches})
                file(SHA256 "${patch}" patch_digest)
                string(APPEND key ";${patch_digest}")
            endforeach()
        endif()
        string(SHA256 digest "${key}")
        string(SUBSTRING "${digest}" 0 16 digest)
        set(source "${STORM_DEPS_CACHE}/fetchcontent/${name}-${digest}")
        if(NOT EXISTS "${source}/.storm-complete")
            file(MAKE_DIRECTORY "${STORM_DEPS_CACHE}/fetchcontent")
            file(LOCK "${source}.lock" GUARD FUNCTION TIMEOUT 3600)
            if(NOT EXISTS "${source}/.storm-complete")
                message(STATUS "Fetching ${name} once into the shared ${source}")
                file(REMOVE_RECURSE "${source}")
                FetchContent_Populate(${name}_shared QUIET ${fetch}
                    SOURCE_DIR "${source}"
                    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/${name}-shared-subbuild"
                    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/${name}-shared-unused")
                foreach(patch ${patches})
                    execute_process(COMMAND git apply "${patch}" WORKING_DIRECTORY "${source}/${patch_dir}"
                        RESULT_VARIABLE patch_result)
                    if(NOT patch_result EQUAL 0)
                        message(FATAL_ERROR "Cannot apply ${patch} to ${source}/${patch_dir}")
                    endif()
                endforeach()
                file(WRITE "${source}/.storm-complete" "${key}\n")
            endif()
        endif()
        if(DEFINED CACHE{FETCHCONTENT_SOURCE_DIR_${upper}} AND NOT "$CACHE{FETCHCONTENT_SOURCE_DIR_${upper}}" STREQUAL ""
           AND NOT "$CACHE{FETCHCONTENT_SOURCE_DIR_${upper}}" STREQUAL "${source}")
            message(WARNING "${name}: the pin or its patches changed; this build directory moves from "
                "$CACHE{FETCHCONTENT_SOURCE_DIR_${upper}} to ${source} (sources recompile once)")
            set(FETCHCONTENT_SOURCE_DIR_${upper} "${source}" CACHE PATH
                "Shared source copy chosen by storm_fetch_declare (cmake/StormBuildCache.cmake)" FORCE)
        endif()
        set(FETCHCONTENT_SOURCE_DIR_${upper} "${source}" PARENT_SCOPE)
    else()
        # An explicit override (offline sources) must carry the same patches: refuse a tree that lacks one.
        set(patches ${arg_STORM_PATCHES})
        if(patches)
            list(POP_FRONT patches patch_dir)
            foreach(patch ${patches})
                execute_process(COMMAND git apply --reverse --check "${patch}"
                    WORKING_DIRECTORY "${FETCHCONTENT_SOURCE_DIR_${upper}}/${patch_dir}"
                    RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
                if(NOT applied EQUAL 0)
                    message(FATAL_ERROR "FETCHCONTENT_SOURCE_DIR_${upper}=${FETCHCONTENT_SOURCE_DIR_${upper}} does not "
                        "carry ${patch}. Drop the override (cmake -U FETCHCONTENT_SOURCE_DIR_${upper} <build dir>: the "
                        "shared patched copy is used) or apply the patch to that tree.")
                endif()
            endforeach()
        endif()
    endif()
    FetchContent_Declare(${name} ${fetch})
endfunction()
