# Host preview and regression harness for the bgfx graphics effects package
# (src/libs/renderer_bgfx/fx). Standalone project: it does not configure the
# engine. See README.md.
cmake_minimum_required(VERSION 3.21)
project(storm-fx-preview LANGUAGES C CXX)
include(FetchContent)

set(STORM_FX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/libs/renderer_bgfx/fx")

# The bgfx pin is owned by src/libs/renderer_bgfx/CMakeLists.txt. Read it from
# there instead of repeating it, so the preview always builds the same bgfx.
file(READ "${STORM_FX_DIR}/../CMakeLists.txt" renderer_cmake)
string(REGEX MATCH "GIT_TAG ([0-9a-f]+)" _ "${renderer_cmake}")
set(bgfx_pin "${CMAKE_MATCH_1}")
if(NOT bgfx_pin)
    message(FATAL_ERROR "Cannot read the bgfx.cmake pin from src/libs/renderer_bgfx/CMakeLists.txt")
endif()

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=/path/to/pinned/checkout
FetchContent_Declare(storm_bgfx_upstream
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG ${bgfx_pin}
    GIT_SUBMODULES bgfx bx bimg
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(storm_bgfx_upstream)
# Same backend as the game on Android: OpenGL ES 3.0 (headless EGL pbuffer here).
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGLES=30)
target_link_libraries(bgfx PUBLIC EGL GLESv2)
foreach(target bgfx bx bimg bimg_decode bimg_encode)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()

set(STORM_FX_BGFX_SHADER_INCLUDE "${storm_bgfx_upstream_SOURCE_DIR}/bgfx/src")
set(STORM_FX_SHADERC_COMMAND "$<TARGET_FILE:shaderc>")
set(STORM_FX_SHADERC_DEPENDS shaderc)
enable_testing()
add_subdirectory("${STORM_FX_DIR}" fx)

# The tool's own shaders: a fixed-function stand-in that includes fx_lighting.sh
# the way the emulator will. Built with the same rules as the package shaders.
set(preview_sc "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(preview_shaders)
storm_fx_add_shader(preview_shaders vs_preview_mesh "${preview_sc}/vs_preview_mesh.sc")
storm_fx_add_shader(preview_shaders fs_preview_mesh "${preview_sc}/fs_preview_mesh.sc")
storm_fx_add_shader(preview_shaders vs_preview_mesh_pp "${preview_sc}/vs_preview_mesh.sc"
    DEFINES FX_LIGHT_PER_PIXEL FX_LIGHT_HEMISPHERE)
storm_fx_add_shader(preview_shaders fs_preview_mesh_pp "${preview_sc}/fs_preview_mesh.sc"
    DEFINES FX_LIGHT_PER_PIXEL FX_LIGHT_HEMISPHERE)
storm_fx_add_shader(preview_shaders fs_preview_mesh_full "${preview_sc}/fs_preview_mesh.sc"
    DEFINES FX_LIGHT_PER_PIXEL FX_LIGHT_HEMISPHERE FX_LIGHT_SPECULAR FX_LIGHT_TRANSLUCENT)
storm_fx_build_shaders(TARGET preview_shaders NAMESPACE preview_shaders VARYING "${preview_sc}/varying.def.sc"
    INCLUDES "${STORM_FX_DIR}/shaders/include" SHADERS ${preview_shaders})

add_executable(fx_preview main.cpp scene.cpp scene.hpp)
target_compile_features(fx_preview PRIVATE cxx_std_20)
target_link_libraries(fx_preview PRIVATE storm_bgfx_fx preview_shaders bgfx bx bimg bimg_encode)
add_custom_target(fx_shader_profiles DEPENDS storm_fx_shaders_all_profiles preview_shaders_all_profiles)
