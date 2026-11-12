# bgfx is the engine's only GPU backend. This directory owns the pinned
# bgfx/bx/bimg dependency, the offline shader build and the D3D9-shaped shim
# (see README.md). Other libraries link `storm_bgfx` and never fetch bgfx.
cmake_minimum_required(VERSION 3.21)
include(FetchContent)
# Third-party code must not inherit the engine's global flags (UNICODE, the
# Windows type headers, -std=c++20 for C sources ...).
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "" COMPILE_DEFINITIONS "" COMPILE_OPTIONS "")
# The engine renders from its main thread; bgfx::frame() executes the backend
# inline, so the EGL context lives on the thread that owns the SDL window.
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_MAX_TEXTURES 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_VERTEX_LAYOUTS 256 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_DYNAMIC_INDEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_VERTEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_INDEX_BUFFERS 8192 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_FRAME_BUFFERS 256 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_TRANSIENT_VERTEX_BUFFER_SIZE 33554432 CACHE STRING "" FORCE)
set(BGFX_CONFIG_MAX_TRANSIENT_INDEX_BUFFER_SIZE 8388608 CACHE STRING "" FORCE)
# Shaders compile with a host shaderc built once per machine and pin (cmake/storm_shaderc.cmake),
# never with bgfx's tools inside this build.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
# The pinned bgfx/bx/bimg.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/bgfx_upstream.cmake")
# Android and the Linux development host both run the GLES 3.0 backend so the
# same ESSL shader blobs and code paths are exercised. Selecting one renderer
# explicitly disables all the others (bgfx/src/config.h).
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGLES=30)
if(ANDROID)
    target_link_libraries(bgfx PUBLIC android log EGL GLESv3)
else()
    target_link_libraries(bgfx PUBLIC EGL GLESv2)
endif()

# storm_bgfx_runtime: what every bgfx user links (never bgfx, bx or bimg directly). On Android it
# is libbgfx.so, the whole of bgfx, bx and bimg in one shared library, so the process has exactly
# one bgfx (one context, one set of handles) for both libengine.so and libstorm_ui.so, and the
# code is in the APK once. The application screens without the engine load it with
# libstorm_ui.so. Elsewhere (desktop engine, host checks) it is the static libraries.
if(ANDROID)
    add_library(storm_bgfx_runtime SHARED runtime/bgfx_runtime.cpp)
    # Next to libengine.so (game/), where the APK packaging takes the engine's libraries from.
    set_target_properties(storm_bgfx_runtime PROPERTIES OUTPUT_NAME bgfx LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
    target_link_libraries(storm_bgfx_runtime PRIVATE -Wl,--whole-archive bgfx bimg bx -Wl,--no-whole-archive)
    target_link_options(storm_bgfx_runtime PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    # The consumers compile against the same headers and configuration (BX_CONFIG_DEBUG ...).
    foreach(part bgfx bimg bx)
        target_include_directories(storm_bgfx_runtime SYSTEM INTERFACE "$<TARGET_PROPERTY:${part},INTERFACE_INCLUDE_DIRECTORIES>")
        target_compile_definitions(storm_bgfx_runtime INTERFACE "$<TARGET_PROPERTY:${part},INTERFACE_COMPILE_DEFINITIONS>")
    endforeach()
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/build_stamp.cmake")
    storm_build_stamp(storm_bgfx_runtime)
else()
    add_library(storm_bgfx_runtime INTERFACE)
    target_link_libraries(storm_bgfx_runtime INTERFACE bgfx bimg bx)
endif()

# etcpak: ETC2 encoder for the DXT -> ETC2 texture transcode (src/texture).
# Only the block compressors are built; no application, image I/O or threading code.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_ETCPAK=/path/to/pinned/checkout
storm_fetch_declare(storm_etcpak
    GIT_REPOSITORY https://github.com/wolfpld/etcpak
    GIT_TAG 3d716e1550023dc5ff8b602636af8b76584e0b66
    STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_etcpak)
add_library(storm_etcpak STATIC
    "${storm_etcpak_SOURCE_DIR}/ProcessRGB.cpp"
    "${storm_etcpak_SOURCE_DIR}/Tables.cpp"
    "${storm_etcpak_SOURCE_DIR}/Dither.cpp")
target_include_directories(storm_etcpak SYSTEM INTERFACE "${storm_etcpak_SOURCE_DIR}")
target_compile_features(storm_etcpak PRIVATE cxx_std_17)
target_compile_options(storm_etcpak PRIVATE -w -O3)
target_compile_definitions(storm_etcpak PRIVATE NDEBUG)
# SSE4.1 is part of the x86-64 Android ABI and of every x86-64 development host;
# arm64 uses etcpak's NEON paths by default.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(storm_etcpak PRIVATE -msse4.1)
endif()
set_target_properties(storm_etcpak PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(cmake/storm_shaderc.cmake)
set(STORM_BGFX_SHADER_PROFILE "300_es" CACHE STRING "shaderc profile for embedded shader blobs")
set(STORM_BGFX_SHADER_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include" CACHE INTERNAL "")
include(cmake/storm_shaders.cmake)

file(GLOB shader_sources CONFIGURE_DEPENDS shaders/*.sc shaders/ports/*.sc)
list(FILTER shader_sources EXCLUDE REGEX "varying\\.def\\.sc$")
# Graphics effects variants of the fixed-function emulator (src/effects/lighting.cpp picks them by name):
# s = FX_LIGHT_SPECULAR, n = FX_LIGHT_NORMALMAP, l = FX_LIGHT_LINEAR, then for fs_ff_lit only the sun shadow:
# h = FX_LIGHT_SHADOW (one tap), o = FX_LIGHT_SHADOW + FX_LIGHT_SHADOW_SOFT. The base sources fs_ff_lit.sc and
# fs_ff_baked.sc are the variants without a suffix.
set(ff_fx_variants)
foreach(suffix _l _s _sl _n _nl _sn _snl _h _lh _sh _slh _nh _nlh _snh _snlh _o _lo _so _slo _no _nlo _sno _snlo)
    set(defines)
    if(suffix MATCHES "s")
        list(APPEND defines FX_LIGHT_SPECULAR)
    endif()
    if(suffix MATCHES "l")
        list(APPEND defines FX_LIGHT_LINEAR)
    endif()
    if(suffix MATCHES "n")
        list(APPEND defines FX_LIGHT_NORMALMAP)
    endif()
    if(suffix MATCHES "[ho]")
        list(APPEND defines FX_LIGHT_SHADOW)
    endif()
    if(suffix MATCHES "o")
        list(APPEND defines FX_LIGHT_SHADOW_SOFT)
    endif()
    list(JOIN defines "," defines)
    list(APPEND ff_fx_variants "fs_ff_lit${suffix}|${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_lit.sc|${defines}")
    if(NOT suffix MATCHES "[sho]")
        list(APPEND ff_fx_variants "fs_ff_baked${suffix}|${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_baked.sc|${defines}")
    endif()
endforeach()
storm_bgfx_embed_shaders(TARGET storm_bgfx_shaders NAMESPACE storm_shaders
    VARYING "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc" SOURCES ${shader_sources}
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/fx/shaders/include" VARIANTS ${ff_fx_variants})

file(GLOB_RECURSE shim_sources CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
list(FILTER shim_sources EXCLUDE REGEX "/src/texture/core/")
add_library(storm_bgfx STATIC ${shim_sources})
target_compile_features(storm_bgfx PUBLIC cxx_std_20)
target_include_directories(storm_bgfx PUBLIC include PRIVATE src)
target_link_libraries(storm_bgfx PUBLIC storm_bgfx_runtime PRIVATE storm_bgfx_shaders ${SDL2_LIBRARIES})
if(ANDROID)
    target_link_libraries(storm_bgfx PRIVATE GLESv3) # driver string and limit queries next to bgfx
    # Swappy GL presents the game's frames (src/shim/frame_pacing.cpp). Only that file sees its headers and the
    # host hooks it reports to (header-only platform/host_hooks.hpp): the other shim sources keep their compile
    # commands (and their ccache entries).
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/swappy.cmake")
    set_source_files_properties(src/shim/frame_pacing.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${storm_swappy_include};${CMAKE_CURRENT_LIST_DIR}/../util/include"
        COMPILE_DEFINITIONS "STORM_SWAPPY_PACKAGE=\"games-frame-pacing ${storm_swappy_version}\"")
    target_link_libraries(storm_bgfx PRIVATE $<LINK_ONLY:storm_swappy>)
    # Swappy registers its JNI methods itself: none of its symbols are exported from the library linking it.
    target_link_options(storm_bgfx INTERFACE "LINKER:--exclude-libs,libswappy_static.a")
endif()
set_target_properties(storm_bgfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(storm_bgfx PRIVATE -fsigned-char)
target_compile_definitions(storm_bgfx PRIVATE NOMINMAX)

# DXT -> ETC2 transcode and on-disk cache (no bgfx dependency; unit-tested on the host).
file(GLOB texture_core_sources CONFIGURE_DEPENDS src/texture/core/*.cpp src/texture/core/*.hpp)
add_library(storm_texture_core STATIC ${texture_core_sources})
target_compile_features(storm_texture_core PUBLIC cxx_std_20)
target_include_directories(storm_texture_core PUBLIC src/texture/core)
target_link_libraries(storm_texture_core PRIVATE storm_etcpak storm_bgfx_runtime)
find_package(Threads REQUIRED)
target_link_libraries(storm_texture_core PUBLIC Threads::Threads)
set_target_properties(storm_texture_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(storm_bgfx PRIVATE storm_texture_core)

# Graphics effects package (fx/README.md): presets «Оригинал / Среднее / Высокое», driven by src/effects.
set(STORM_FX_SHADERC_COMMAND "${STORM_SHADERC_COMMAND}")
set(STORM_FX_SHADERC_DEPENDS "${STORM_SHADERC_DEPENDS}")
set(STORM_FX_BGFX_SHADER_INCLUDE "${STORM_BGFX_SOURCE_DIR}/bgfx/src")
add_subdirectory(fx)
target_link_libraries(storm_bgfx PUBLIC storm_bgfx_fx)

# Texture transcode tests and benchmark: only on request, so projects that merely consume
# storm_bgfx (e.g. the standalone UI build) do not build them.
option(STORM_BGFX_BUILD_TESTS "Build the renderer_bgfx texture tests and benchmark" OFF)
if(STORM_BGFX_BUILD_TESTS)
    add_executable(storm_texture_bench tests/texture/bench.cpp)
    target_link_libraries(storm_texture_bench PRIVATE storm_texture_core)
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(storm_texture_tests tests/texture/tests.cpp)
        target_link_libraries(storm_texture_tests PRIVATE storm_texture_core bimg_decode bimg bx)
        enable_testing()
        add_test(NAME storm_texture_tests COMMAND storm_texture_tests)
    endif()
endif()
