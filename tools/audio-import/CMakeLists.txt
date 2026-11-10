# Host CLI for src/libs/audio_import (usage: src/libs/audio_import/README.md).
#   cmake -S tools/audio-import -B build/audio-import -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/audio-import
cmake_minimum_required(VERSION 3.21)
project(storm_audio_import_tool LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/audio_import" audio_import)
add_executable(storm-audio-import main.cpp)
target_link_libraries(storm-audio-import PRIVATE audio_import)
target_compile_options(storm-audio-import PRIVATE -Wall -Wextra)
