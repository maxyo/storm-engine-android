# storm_bgfx_embed_shaders(TARGET <interface target> NAMESPACE <id> VARYING <varying.def.sc>
#                          SOURCES vs_*.sc fs_*.sc ... [DEFINES "A;B=1"] [INCLUDES dir...]
#                          [VARIANTS "<name>|<source>|A,B=1" ...])
# Compiles each bgfx shader with the pinned host shaderc for both backends of the engine: OpenGL ES
# (STORM_BGFX_SHADER_PROFILE, `<name>.bin.h`, a `static const uint8_t <name>[]`) and Vulkan (profile spirv,
# `<name>.spv.bin.h`, `<name>_spv[]`), and exposes them through the INTERFACE target's include path as
# "<NAMESPACE>/<name>.bin.h" / "<NAMESPACE>/<name>.spv.bin.h". includes.inc pulls in both of every shader;
# storm/render/shader_blob.hpp (STORM_SHADER_BLOB) picks the one of the running backend.
# File names must start with vs_ or fs_; a variant compiles <source> again with extra
# defines under its own <name>. Other libraries (e.g. the UI) call this with their own
# sources and varying.def.sc; see renderer_bgfx/README.md. A source's STORM_SHADER_VARYING property can
# override VARYING when its vertex attributes need a different type (for example packed SHORT4 grass).
set(STORM_SHADER_PRECISION_CHECK "${CMAKE_CURRENT_LIST_DIR}/shader_precision_check.py" CACHE INTERNAL "")
function(storm_bgfx_embed_shaders)
    if(NOT STORM_SHADERC_LAUNCHER)
        message(FATAL_ERROR "STORM_SHADERC_LAUNCHER is not set: include renderer_bgfx/cmake/storm_shaderc.cmake first")
    endif()
    cmake_parse_arguments(arg "" "TARGET;NAMESPACE;VARYING" "SOURCES;DEFINES;INCLUDES;VARIANTS" ${ARGN})
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen/${arg_NAMESPACE}")
    file(MAKE_DIRECTORY "${out_dir}")
    set(outputs)
    set(include_lines "")
    set(table_lines "")
    set(include_args -i "${STORM_BGFX_SOURCE_DIR}/src" -i "${STORM_BGFX_SHADER_INCLUDE}")
    file(GLOB common_deps "${STORM_BGFX_SHADER_INCLUDE}/*")
    foreach(dir ${arg_INCLUDES})
        list(APPEND include_args -i "${dir}")
        file(GLOB dir_deps "${dir}/*")
        list(APPEND common_deps ${dir_deps})
    endforeach()
    set(jobs)
    foreach(source ${arg_SOURCES})
        get_filename_component(name "${source}" NAME_WE)
        list(APPEND jobs "${name}|${source}|")
    endforeach()
    list(APPEND jobs ${arg_VARIANTS})
    foreach(job ${jobs})
        string(REPLACE "|" ";" fields "${job}")
        list(GET fields 0 name)
        list(GET fields 1 source)
        list(LENGTH fields field_count)
        set(variant_defines)
        if(field_count GREATER 2)
            list(GET fields 2 variant_defines)
            string(REPLACE "," ";" variant_defines "${variant_defines}")
        endif()
        if(name MATCHES "^vs_")
            set(type vertex)
        elseif(name MATCHES "^fs_")
            set(type fragment)
        else()
            message(FATAL_ERROR "Shader file must be named vs_* or fs_*: ${source}")
        endif()
        set(output "${out_dir}/${name}.bin.h")
        set(output_spirv "${out_dir}/${name}.spv.bin.h")
        set(define_args)
        set(all_defines ${arg_DEFINES} ${variant_defines})
        if(all_defines)
            list(JOIN all_defines "$<SEMICOLON>" joined) # one shaderc argument
            set(define_args --define "${joined}")
        endif()
        get_source_file_property(varying "${source}" STORM_SHADER_VARYING)
        if(NOT varying)
            set(varying "${arg_VARYING}")
        endif()
        set(common --type ${type} --platform android -O 3 --varyingdef "${varying}" ${include_args} ${define_args})
        set(deps ${STORM_SHADERC_DEPENDS} "${source}" "${varying}" ${common_deps} "${STORM_SHADERC_LAUNCHER_SCRIPT}")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${STORM_SHADERC_LAUNCHER} "${STORM_SHADERC_COMMAND}" -f "${source}" -o "${output}" --bin2c "${name}"
                -p ${STORM_BGFX_SHADER_PROFILE} ${common}
            DEPENDS ${deps} VERBATIM)
        add_custom_command(OUTPUT "${output_spirv}"
            COMMAND ${STORM_SHADERC_LAUNCHER} "${STORM_SHADERC_COMMAND}" -f "${source}" -o "${output_spirv}" --bin2c "${name}_spv"
                -p spirv ${common}
            DEPENDS ${deps} VERBATIM)
        list(APPEND outputs "${output}" "${output_spirv}")
        string(APPEND include_lines "#include \"${arg_NAMESPACE}/${name}.bin.h\"\n#include \"${arg_NAMESPACE}/${name}.spv.bin.h\"\n")
        string(APPEND table_lines "STORM_SHADER(${name})\n")
    endforeach()
    # includes.inc pulls in every blob (both backends); table.inc lists STORM_SHADER(name) entries.
    file(GENERATE OUTPUT "${out_dir}/includes.inc" CONTENT "${include_lines}")
    file(GENERATE OUTPUT "${out_dir}/table.inc" CONTENT "${table_lines}")
    # Every blob is highp (cmake/shader_precision_check.py): the build fails otherwise.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(precision_stamp "${out_dir}/precision-check.stamp")
    add_custom_command(OUTPUT "${precision_stamp}"
        COMMAND Python3::Interpreter "${STORM_SHADER_PRECISION_CHECK}" "${precision_stamp}" ${outputs}
        DEPENDS ${outputs} "${STORM_SHADER_PRECISION_CHECK}" VERBATIM)
    add_custom_target(${arg_TARGET}_build DEPENDS ${outputs} "${precision_stamp}")
    add_library(${arg_TARGET} INTERFACE)
    add_dependencies(${arg_TARGET} ${arg_TARGET}_build)
    target_include_directories(${arg_TARGET} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen")
endfunction()
