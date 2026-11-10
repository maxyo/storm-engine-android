# Effekseer particle effects on bgfx (see README.md). Added by renderer_bgfx after bgfx exists;
# uses its STORM_SHADERC_COMMAND / storm_bgfx_embed_shaders pipeline for the efkbgfx shaders.
#
#   storm_effekseer_runtime  Effekseer core + EffekseerRendererCommon (pinned upstream release)
#   storm_efkbgfx            the vendored bgfx renderer (efkbgfx/, ported to Effekseer 1.80)
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Effekseer 1.80h (tag 1808). Immutable revision and archive digest are checked in.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_EFFEKSEER=/path/to/an/extracted/copy/of/this/revision
# SOURCE_SUBDIR points at a directory without a CMakeLists.txt: only the runtime sources below are
# built, never the editor/viewer/tool projects of the upstream tree.
FetchContent_Declare(storm_effekseer
    URL https://codeload.github.com/effekseer/Effekseer/tar.gz/b5700082b49bc5f919610880dafddc90211a9a4e
    URL_HASH SHA256=fe3d4883389eebf96698f35b303b1672210a9a74cf22b594d81a37fb01aa6a58
    SOURCE_SUBDIR runtime-sources-only)
FetchContent_MakeAvailable(storm_effekseer)
set(efk_dir "${storm_effekseer_SOURCE_DIR}/Dev/Cpp")

# Network (the editor's live-preview server) stays disabled; texture decoding uses the bundled stb.
file(GLOB_RECURSE efk_core_sources CONFIGURE_DEPENDS "${efk_dir}/Effekseer/Effekseer/*.cpp")
list(FILTER efk_core_sources EXCLUDE REGEX "/Network/")
file(GLOB efk_common_sources CONFIGURE_DEPENDS "${efk_dir}/EffekseerRendererCommon/EffekseerRendererCommon/*.cpp")
add_library(storm_effekseer_runtime STATIC ${efk_core_sources} ${efk_common_sources})
target_include_directories(storm_effekseer_runtime SYSTEM PUBLIC
    "${efk_dir}/Effekseer" "${efk_dir}/EffekseerRendererCommon")
target_compile_features(storm_effekseer_runtime PUBLIC cxx_std_17)
target_compile_options(storm_effekseer_runtime PRIVATE -w)
find_package(Threads REQUIRED)
target_link_libraries(storm_effekseer_runtime PUBLIC Threads::Threads)
set_target_properties(storm_effekseer_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# efkbgfx shaders: one storm_bgfx_embed_shaders group per varying definition. Sprites reuse the
# fragment shaders of the matching model group.
set(efk_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/efkbgfx/shaders")
set(efk_shader_targets)
foreach(kind unlit lit distortion)
    foreach(prefix sprite sprite_adv model model_adv)
        set(sources "${efk_shader_dir}/vs_efk_${prefix}_${kind}.sc")
        if(prefix MATCHES "^model")
            list(APPEND sources "${efk_shader_dir}/fs_efk_${prefix}_${kind}.sc")
        endif()
        storm_bgfx_embed_shaders(TARGET storm_efk_shaders_${prefix}_${kind} NAMESPACE efk_${prefix}_${kind}
            VARYING "${efk_shader_dir}/varying_${prefix}_${kind}.def.sc" SOURCES ${sources}
            INCLUDES "${efk_shader_dir}")
        list(APPEND efk_shader_targets storm_efk_shaders_${prefix}_${kind})
    endforeach()
endforeach()

add_library(storm_efkbgfx STATIC efkbgfx/renderer/bgfxrenderer.cpp efkbgfx/renderer/shaders.cpp)
target_include_directories(storm_efkbgfx PUBLIC efkbgfx/renderer)
target_link_libraries(storm_efkbgfx PUBLIC storm_effekseer_runtime bgfx bx PRIVATE bimg ${efk_shader_targets})
target_compile_features(storm_efkbgfx PUBLIC cxx_std_17)
set_target_properties(storm_efkbgfx PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The engine-facing system (include/storm/render/effekseer*.hpp). It depends on bgfx only, so the
# tests drive it without the D3D9-shaped shim; the shim attaches the device and opens the view.
add_library(storm_effekseer STATIC src/system.cpp)
target_include_directories(storm_effekseer PUBLIC ../include)
target_link_libraries(storm_effekseer PRIVATE storm_efkbgfx)
target_compile_features(storm_effekseer PUBLIC cxx_std_20)
set_target_properties(storm_effekseer PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Host checks against a game directory (tests/effects_check.cpp). The CTest entries exist only when
# STORM_CL_GAME_DIR is set and report SKIP when it is missing; game files never enter the repository.
if(STORM_BGFX_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    add_executable(storm_effekseer_check tests/effects_check.cpp)
    target_link_libraries(storm_effekseer_check PRIVATE storm_effekseer bgfx bimg bx)
    set(STORM_CL_GAME_DIR "" CACHE PATH "Caribbean Legend game directory for the Effekseer checks")
    if(STORM_CL_GAME_DIR)
        enable_testing()
        add_test(NAME effekseer_cl_load COMMAND storm_effekseer_check load "${STORM_CL_GAME_DIR}")
        add_test(NAME effekseer_cl_render COMMAND storm_effekseer_check render "${STORM_CL_GAME_DIR}"
            CLTorchFire.efkefc "${CMAKE_CURRENT_BINARY_DIR}/effekseer_render.png")
        set_tests_properties(effekseer_cl_load effekseer_cl_render PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
