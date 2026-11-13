# storm_fx_add_shader(<list-var> <name> <source> [DEFINES A B=1 ...])
#
# Registers one shader variant. <name> must start with vs_ or fs_; several
# variants may share a source and differ only in their defines.
#
# storm_fx_build_shaders(TARGET <interface target> NAMESPACE <id> VARYING <varying.def.sc>
#                        INCLUDES <dir>... SHADERS <entries>)
#
# Compiles every registered variant with the pinned bgfx shaderc:
#   * for both backends of the engine as `--bin2c` headers under <build>/shaders-gen/<NAMESPACE>/:
#     <name>.bin.h (OpenGL ES, STORM_FX_SHADER_PROFILE, default 300_es, array <name>) and <name>.spv.bin.h
#     (Vulkan, profile spirv, array <name>_spv), exposed through the INTERFACE target together with
#     includes.inc (every blob) and table.inc (one FX_SHADER(name) line per variant);
#     storm/render/shader_blob.hpp picks the blob of the running backend;
#   * target `<TARGET>_all_profiles` additionally compiles each variant for Metal so that no shader
#     silently loses that backend.
function(storm_fx_add_shader list_var name source)
    cmake_parse_arguments(arg "" "" "DEFINES" ${ARGN})
    string(REPLACE ";" "," defines "${arg_DEFINES}")
    set(${list_var} ${${list_var}} "${name}|${source}|${defines}" PARENT_SCOPE)
endfunction()

function(storm_fx_build_shaders)
    if(NOT STORM_SHADERC_LAUNCHER)
        message(FATAL_ERROR "STORM_SHADERC_LAUNCHER is not set: include renderer_bgfx/cmake/storm_shaderc.cmake first")
    endif()
    cmake_parse_arguments(arg "" "TARGET;NAMESPACE;VARYING" "INCLUDES;SHADERS" ${ARGN})
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen/${arg_NAMESPACE}")
    set(check_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders-check/${arg_NAMESPACE}")
    file(MAKE_DIRECTORY "${gen_dir}" "${check_dir}/metal")
    set(include_args -i "${STORM_FX_BGFX_SHADER_INCLUDE}")
    set(header_deps)
    foreach(dir ${arg_INCLUDES})
        list(APPEND include_args -i "${dir}")
        file(GLOB dir_headers CONFIGURE_DEPENDS "${dir}/*.sh")
        list(APPEND header_deps ${dir_headers})
    endforeach()
    set(outputs)
    set(check_outputs)
    set(includes "")
    set(table "")
    foreach(entry ${arg_SHADERS})
        string(REPLACE "|" ";" fields "${entry}|")
        list(GET fields 0 name)
        list(GET fields 1 source)
        list(GET fields 2 defines)
        if(name MATCHES "^vs_")
            set(type vertex)
        elseif(name MATCHES "^fs_")
            set(type fragment)
        else()
            message(FATAL_ERROR "fx shader variant must be named vs_* or fs_*: ${name}")
        endif()
        # FX_SHADER is always defined so the option never has an empty value; the
        # list is passed as one quoted argument (shaderc expects "A;B").
        string(REPLACE "," "$<SEMICOLON>" defines "FX_SHADER,${defines}")
        set(common --type ${type} --varyingdef "${arg_VARYING}" ${include_args})
        set(deps ${STORM_FX_SHADERC_DEPENDS} "${source}" "${arg_VARYING}" ${header_deps} "${STORM_SHADERC_LAUNCHER_SCRIPT}")
        set(output "${gen_dir}/${name}.bin.h")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${STORM_SHADERC_LAUNCHER} "${STORM_FX_SHADERC_COMMAND}" -f "${source}" -o "${output}" --bin2c "${name}"
                --platform android -p ${STORM_FX_SHADER_PROFILE} -O 3 ${common} --define "${defines}"
            DEPENDS ${deps} VERBATIM)
        list(APPEND outputs "${output}")
        set(output_spirv "${gen_dir}/${name}.spv.bin.h")
        add_custom_command(OUTPUT "${output_spirv}"
            COMMAND ${STORM_SHADERC_LAUNCHER} "${STORM_FX_SHADERC_COMMAND}" -f "${source}" -o "${output_spirv}" --bin2c "${name}_spv"
                --platform android -p spirv -O 3 ${common} --define "${defines}"
            DEPENDS ${deps} VERBATIM)
        list(APPEND outputs "${output_spirv}")
        add_custom_command(OUTPUT "${check_dir}/metal/${name}.bin"
            COMMAND ${STORM_SHADERC_LAUNCHER} "${STORM_FX_SHADERC_COMMAND}" -f "${source}" -o "${check_dir}/metal/${name}.bin"
                --platform ios -p metal ${common} --define "${defines}"
            DEPENDS ${deps} VERBATIM)
        list(APPEND check_outputs "${check_dir}/metal/${name}.bin")
        string(APPEND includes "#include \"${arg_NAMESPACE}/${name}.bin.h\"\n#include \"${arg_NAMESPACE}/${name}.spv.bin.h\"\n")
        string(APPEND table "FX_SHADER(${name})\n")
    endforeach()
    file(GENERATE OUTPUT "${gen_dir}/includes.inc" CONTENT "${includes}")
    file(GENERATE OUTPUT "${gen_dir}/table.inc" CONTENT "${table}")
    # Every blob is highp (renderer_bgfx/cmake/shader_precision_check.py) when the renderer provides the check.
    if(STORM_SHADER_PRECISION_CHECK)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(precision_stamp "${gen_dir}/precision-check.stamp")
        add_custom_command(OUTPUT "${precision_stamp}"
            COMMAND Python3::Interpreter "${STORM_SHADER_PRECISION_CHECK}" "${precision_stamp}" ${outputs}
            DEPENDS ${outputs} "${STORM_SHADER_PRECISION_CHECK}" VERBATIM)
        list(APPEND outputs "${precision_stamp}")
    endif()
    add_custom_target(${arg_TARGET}_build DEPENDS ${outputs})
    add_custom_target(${arg_TARGET}_all_profiles DEPENDS ${check_outputs})
    add_library(${arg_TARGET} INTERFACE)
    add_dependencies(${arg_TARGET} ${arg_TARGET}_build)
    target_include_directories(${arg_TARGET} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/shaders-gen")
endfunction()
