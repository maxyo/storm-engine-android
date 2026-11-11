# Full engine dependencies; all sources/toolchains are supplied explicitly.
foreach(path STORM_ANDROID_DEPS STORM_ANDROID_SOURCES STORM_AUDIO_SOURCES)
    if(NOT IS_DIRECTORY "${${path}}")
        message(FATAL_ERROR "Set ${path} to a prepared dependency directory")
    endif()
endforeach()
if(STORM_ENABLE_CRASH_REPORTS)
    message(FATAL_ERROR "Android currently requires crash reports OFF")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
add_compile_options(-fsigned-char)
find_package(fmt REQUIRED CONFIG PATHS "${STORM_ANDROID_DEPS}/lib/cmake/fmt" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
add_library(fmt ALIAS fmt::fmt)
add_library(sdl SHARED IMPORTED GLOBAL)
set_target_properties(sdl PROPERTIES IMPORTED_LOCATION "${STORM_ANDROID_DEPS}/lib/libSDL2.so" INTERFACE_INCLUDE_DIRECTORIES "${STORM_ANDROID_DEPS}/include;${STORM_ANDROID_DEPS}/include/SDL2")
set(SDL2_LIBRARIES sdl)
add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE "${STORM_ANDROID_SOURCES}/fast_float/include")
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE "${STORM_ANDROID_SOURCES}/spdlog/include")
target_compile_definitions(spdlog INTERFACE SPDLOG_FMT_EXTERNAL)
target_link_libraries(spdlog INTERFACE fmt)
add_library(catch2 INTERFACE)
target_include_directories(catch2 INTERFACE "${STORM_ANDROID_SOURCES}/catch2/single_include")
# Catch2 writes its output to the Android log (__android_log_write) on Android.
target_link_libraries(catch2 INTERFACE log)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
# Android API 24 uses emulated TLS. Its first allocation calls libc malloc;
# interposing malloc with mimalloc re-enters the uninitialized heap recursively.
# Keep both C and C++ allocations on Bionic on Android: shared libc++ may
# free storage internally, so mixing its heap with an engine-only new override
# also corrupts allocator ownership. Explicit mi_* diagnostics remain available.
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
add_subdirectory("${STORM_ANDROID_SOURCES}/mimalloc" "${CMAKE_BINARY_DIR}/mimalloc")
set_target_properties(mimalloc PROPERTIES OUTPUT_NAME mimalloc VERSION "" SOVERSION "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")
# Release = the RelWithDebInfo flags (-O2 -g -DNDEBUG, set in src/CMakeLists.txt)
# plus hidden visibility, section GC and ThinLTO for the engine's own targets
# (declared after this point; mimalloc above keeps its exported C API).
# Hidden visibility drops ~11.5k C++ symbols from .dynsym, which shortens
# load-time symbol resolution and lets the linker discard and inline across
# modules. What must stay exported is marked in the sources: SDL_main
# (SDLMAIN_DECLSPEC) and the JNI entry points (JNIEXPORT). Debug info is kept
# so the application's release packaging can split symbols from the stripped
# library. -DSTORM_ANDROID_RELEASE_LTO=OFF keeps everything but LTO.
option(STORM_ANDROID_RELEASE_LTO "ThinLTO for Android Release builds" ON)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
    if(STORM_ANDROID_RELEASE_LTO)
        add_compile_options(-flto=thin)
        add_link_options(-flto=thin)
    endif()
endif()
# NDK exposes the Android platform zlib ABI. Do not package a competing libz.so.
add_library(zlib INTERFACE)
target_link_libraries(zlib INTERFACE z)
add_library(storm_open_audio STATIC "${CMAKE_SOURCE_DIR}/android/audio/implementation.cpp" "${CMAKE_SOURCE_DIR}/android/audio/service_backend.cpp")
target_include_directories(storm_open_audio PUBLIC "${CMAKE_SOURCE_DIR}/android/audio" "${STORM_AUDIO_SOURCES}")
target_link_libraries(storm_open_audio PUBLIC dl m)
# Short ABI name of the toolchain file (cmake/toolchains/android-<arch>.cmake): arm64 for every
# release, x86_64 for emulator debug builds.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(STORM_ANDROID_ARCH arm64)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(STORM_ANDROID_ARCH x86_64)
else()
    message(FATAL_ERROR "Unsupported ANDROID_ABI ${ANDROID_ABI} (arm64-v8a or x86_64)")
endif()
include(ffmpeg)
storm_ffmpeg("${STORM_ANDROID_SOURCES}" android-${STORM_ANDROID_ARCH})
execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE STORM_ANDROID_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
string(TOUPPER "${STORM_ANDROID_ARCH}" storm_watermark_arch)
file(WRITE "${STORM_WATERMARK_FILE}" "#pragma once\n#define STORM_BUILD_WATERMARK Android_${storm_watermark_arch}_${STORM_ANDROID_REVISION}_open_audio\n")
