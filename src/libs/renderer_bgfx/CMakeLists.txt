if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(storm-bgfx-stage1 LANGUAGES C CXX)
    set(STORM_ENABLE_BGFX ON)
endif()
option(STORM_ENABLE_BGFX "Build experimental bgfx world passes (not a complete game renderer)" OFF)
if(NOT STORM_ENABLE_BGFX)
    return()
endif()
if(CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR "The experimental bgfx target requires CMake 3.21 or newer")
endif()

include(FetchContent)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
if(CMAKE_CROSSCOMPILING)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
endif()
# This upstream commit pins bgfx, bx and bimg as gitlinks. Do not track master.
FetchContent_Declare(storm_bgfx_upstream
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG 8d950d6dc3df1d763e7d3655987c89009767a07e
    GIT_SUBMODULES bgfx bx bimg
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(storm_bgfx_upstream)

add_library(storm_bgfx_world STATIC src/fog_pass.cpp)
target_compile_features(storm_bgfx_world PUBLIC cxx_std_20)
target_include_directories(storm_bgfx_world PUBLIC include ../weather/include)
target_link_libraries(storm_bgfx_world PUBLIC bgfx)
set_target_properties(storm_bgfx_world PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    target_link_libraries(storm_bgfx_world PUBLIC android log EGL GLESv3)
    # Force a final ARM64 link, not merely an archive with unresolved symbols.
    # This probe is never packaged into the stable APK.
    add_library(storm_bgfx_link_check SHARED tests/android_link_check.cpp)
    target_link_libraries(storm_bgfx_link_check PRIVATE storm_bgfx_world)
    target_link_options(storm_bgfx_link_check PRIVATE -Wl,--no-undefined)
elseif(NOT CMAKE_CROSSCOMPILING)
    set(shader_outputs)
    foreach(stage vs fs)
        if(stage STREQUAL vs)
            set(shader_type vertex)
        else()
            set(shader_type fragment)
        endif()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/${stage}_fog.bin")
        add_custom_command(OUTPUT "${output}"
            COMMAND shaderc -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${stage}_fog.sc"
                -o "${output}" --type ${shader_type} --platform linux -p spirv
                --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc"
                -i "${storm_bgfx_upstream_SOURCE_DIR}/bgfx/src"
            DEPENDS shaderc shaders/${stage}_fog.sc shaders/varying.def.sc
            VERBATIM)
        list(APPEND shader_outputs "${output}")
    endforeach()
    add_custom_target(storm_bgfx_shaders DEPENDS ${shader_outputs})
    add_executable(storm_bgfx_fog_check tests/fog_check.cpp)
    target_link_libraries(storm_bgfx_fog_check PRIVATE storm_bgfx_world bx)
    add_dependencies(storm_bgfx_fog_check storm_bgfx_shaders)
    enable_testing()
    add_test(NAME bgfx_fog_pixels COMMAND storm_bgfx_fog_check
        "${CMAKE_CURRENT_BINARY_DIR}/vs_fog.bin" "${CMAKE_CURRENT_BINARY_DIR}/fs_fog.bin")
endif()
