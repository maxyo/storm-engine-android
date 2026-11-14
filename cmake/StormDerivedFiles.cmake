# storm_derived_files(): files a pinned Python script makes at build time from pinned downloads, instead of being kept in
# the repository (the owner's rule for the relief models: no .onnx files in the repository). Made once per machine
# into ${STORM_DEPS_CACHE}/derived/<name>-<digest>, a read-only cache shared by every build directory and worktree; the
# digest covers the script, its requirements, its arguments, the inputs' digests and the outputs' expected digests, so
# any change makes them again. Every output is checked against its expected sha256: a toolchain that makes other bytes
# fails the build loudly (nothing is kept).
#
#   storm_derived_files(<target> <name>
#       SCRIPT <script.py>            the script, run as: python <script> --sources <dir>... --output <dir> [ARGS...]
#       REQUIREMENTS <requirements>   exact pins (pip install --no-deps --only-binary=:all: -r), the venv's whole content
#       SOURCES <dir>...              the folders of the inputs (storm_fetch_declare downloads)
#       INPUTS <sha256>...            the inputs' digests (part of the cache digest; the script checks the files)
#       OUTPUTS <file>=<sha256>...    the files it makes and their digests
#       [FILES <file>...]             more files the script reads (its modules): part of the digest and the dependencies
#       [ARGS <arg>...] [PIP_ARGS <arg>...])
# defines the custom target <target> that makes them and sets <name>_DIR (the folder with the outputs) in the caller's
# scope. The venv lives in ${STORM_DEPS_CACHE}/python/<digest of the requirements and the Python>; pip downloads its
# wheels, or takes them from the folder STORM_PIP_WHEELHOUSE (environment or cache variable: offline, no index).
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/StormBuildCache.cmake")
set(STORM_DERIVED_RUNNER "${CMAKE_CURRENT_LIST_DIR}/StormDerivedRun.cmake")

function(storm_derived_files target name)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "SCRIPT;REQUIREMENTS" "SOURCES;INPUTS;OUTPUTS;ARGS;PIP_ARGS;FILES")
    if(NOT arg_SCRIPT OR NOT arg_REQUIREMENTS OR NOT arg_SOURCES OR NOT arg_OUTPUTS)
        message(FATAL_ERROR "storm_derived_files(${target} ${name}): SCRIPT, REQUIREMENTS, SOURCES and OUTPUTS are required")
    endif()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    file(SHA256 "${arg_SCRIPT}" script_digest)
    file(SHA256 "${arg_REQUIREMENTS}" requirements_digest)
    set(files_digest "")
    foreach(extra ${arg_FILES})
        file(SHA256 "${extra}" extra_digest)
        string(APPEND files_digest "${extra_digest};")
    endforeach()
    string(SHA256 digest "${script_digest};${requirements_digest};${files_digest};${arg_ARGS};${arg_PIP_ARGS};${arg_INPUTS};${arg_OUTPUTS}")
    string(SUBSTRING "${digest}" 0 16 digest)
    set(dir "${STORM_DEPS_CACHE}/derived/${name}-${digest}")
    set(outputs "")
    foreach(pair ${arg_OUTPUTS})
        if(NOT pair MATCHES "^([^=]+)=([0-9a-f]+)$")
            message(FATAL_ERROR "storm_derived_files(${target}): OUTPUTS takes <file>=<sha256>, got '${pair}'")
        endif()
        list(APPEND outputs "${dir}/${CMAKE_MATCH_1}")
    endforeach()
    if(NOT DEFINED STORM_PIP_WHEELHOUSE AND DEFINED ENV{STORM_PIP_WHEELHOUSE})
        set(STORM_PIP_WHEELHOUSE "$ENV{STORM_PIP_WHEELHOUSE}")
    endif()
    string(REPLACE ";" "|" outputs_arg "${arg_OUTPUTS}")
    string(REPLACE ";" "|" args_arg "${arg_ARGS}")
    string(REPLACE ";" "|" pip_arg "${arg_PIP_ARGS}")
    string(REPLACE ";" "|" sources_arg "${arg_SOURCES}")
    add_custom_command(OUTPUT ${outputs} "${dir}/.storm-complete"
        COMMAND "${CMAKE_COMMAND}" "-DNAME=${name}" "-DDIR=${dir}" "-DPYTHON=${Python3_EXECUTABLE}"
                "-DSCRIPT=${arg_SCRIPT}" "-DREQUIREMENTS=${arg_REQUIREMENTS}" "-DSOURCES=${sources_arg}"
                "-DOUTPUTS=${outputs_arg}" "-DARGS=${args_arg}" "-DPIP_ARGS=${pip_arg}"
                "-DPYTHON_CACHE=${STORM_DEPS_CACHE}/python" "-DWHEELHOUSE=${STORM_PIP_WHEELHOUSE}"
                -P "${STORM_DERIVED_RUNNER}"
        DEPENDS "${arg_SCRIPT}" "${arg_REQUIREMENTS}" ${arg_FILES} "${STORM_DERIVED_RUNNER}"
        COMMENT "Making ${name} (once per machine: ${dir})"
        VERBATIM USES_TERMINAL)
    add_custom_target(${target} DEPENDS ${outputs} "${dir}/.storm-complete")
    set(${name}_DIR "${dir}" PARENT_SCOPE)
endfunction()
