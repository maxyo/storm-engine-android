# storm_bgfx_embed_shaders(TARGET <interface target> NAMESPACE <id> VARYING <varying.def.sc>
#                          SOURCES vs_*.sc fs_*.sc ... [DEFINES "A;B=1"] [INCLUDES dir...])
# Compiles each bgfx shader with the pinned host shaderc for STORM_BGFX_SHADER_PROFILE
# and exposes `<build>/<NAMESPACE>/<name>.bin.h` (a `static const uint8_t <name>[]`)
# through the INTERFACE target's include path as "<NAMESPACE>/<name>.bin.h".
# File names must start with vs_ or fs_. Other libraries (e.g. the UI) call
# this with their own sources and varying.def.sc; see renderer_bgfx/README.md.
function(storm_bgfx_embed_shaders)
    cmake_parse_arguments(arg "" "TARGET;NAMESPACE;VARYING" "SOURCES;DEFINES;INCLUDES" ${ARGN})
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen/${arg_NAMESPACE}")
    file(MAKE_DIRECTORY "${out_dir}")
    set(outputs)
    set(include_lines "")
    set(table_lines "")
    set(include_args -i "${STORM_BGFX_SOURCE_DIR}/bgfx/src" -i "${STORM_BGFX_SHADER_INCLUDE}")
    foreach(dir ${arg_INCLUDES})
        list(APPEND include_args -i "${dir}")
    endforeach()
    file(GLOB common_deps "${STORM_BGFX_SHADER_INCLUDE}/*")
    foreach(source ${arg_SOURCES})
        get_filename_component(name "${source}" NAME_WE)
        if(name MATCHES "^vs_")
            set(type vertex)
        elseif(name MATCHES "^fs_")
            set(type fragment)
        else()
            message(FATAL_ERROR "Shader file must be named vs_* or fs_*: ${source}")
        endif()
        set(output "${out_dir}/${name}.bin.h")
        set(define_args)
        if(arg_DEFINES)
            list(JOIN arg_DEFINES ";" joined)
            set(define_args --define "${joined}")
        endif()
        add_custom_command(OUTPUT "${output}"
            COMMAND "${STORM_SHADERC_COMMAND}" -f "${source}" -o "${output}" --bin2c "${name}"
                --type ${type} --platform android -p ${STORM_BGFX_SHADER_PROFILE} -O 3
                --varyingdef "${arg_VARYING}" ${include_args} ${define_args}
            DEPENDS ${STORM_SHADERC_DEPENDS} "${source}" "${arg_VARYING}" ${common_deps}
            VERBATIM)
        list(APPEND outputs "${output}")
        string(APPEND include_lines "#include \"${arg_NAMESPACE}/${name}.bin.h\"\n")
        string(APPEND table_lines "STORM_SHADER(${name})\n")
    endforeach()
    # includes.inc pulls in every blob; table.inc lists STORM_SHADER(name) entries.
    file(GENERATE OUTPUT "${out_dir}/includes.inc" CONTENT "${include_lines}")
    file(GENERATE OUTPUT "${out_dir}/table.inc" CONTENT "${table_lines}")
    add_custom_target(${arg_TARGET}_build DEPENDS ${outputs})
    add_library(${arg_TARGET} INTERFACE)
    add_dependencies(${arg_TARGET} ${arg_TARGET}_build)
    target_include_directories(${arg_TARGET} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen")
endfunction()
