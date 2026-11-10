# Synthetic checks for src/libs/audio_import; driven by run.py.
cmake_minimum_required(VERSION 3.21)
project(storm_audio_import_check LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/audio_import" audio_import)
include(FetchContent)
# Same stb_vorbis revision as tests/audio/run.py and the sound backend.
FetchContent_Declare(storm_stb_vorbis
    URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c
    URL_HASH SHA256=4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(storm_stb_vorbis)
add_library(check_stb_vorbis STATIC stb_vorbis_impl.c)
target_include_directories(check_stb_vorbis PUBLIC "${storm_stb_vorbis_SOURCE_DIR}")
target_compile_options(check_stb_vorbis PRIVATE -w)
add_executable(audio-import-check check.cpp)
target_link_libraries(audio-import-check PRIVATE audio_import check_stb_vorbis)
target_compile_options(audio-import-check PRIVATE -Wall -Wextra -Werror)
target_compile_options(audio_import PRIVATE -Werror)
# The CLI from tools/audio-import, for the optional local game-data run.
add_executable(storm-audio-import "${CMAKE_CURRENT_LIST_DIR}/../../tools/audio-import/main.cpp")
target_link_libraries(storm-audio-import PRIVATE audio_import)
