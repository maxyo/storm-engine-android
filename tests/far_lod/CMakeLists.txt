# Host check of the far LODs (src/libs/geometry/far_lod) on synthetic models (check.cpp, verify.hpp).
# Configured by run.py.
cmake_minimum_required(VERSION 3.21)
project(storm_far_lod_check LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/geometry/far_lod" far_lod)
add_executable(far_lod_check check.cpp)
target_compile_options(far_lod_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(far_lod_check PRIVATE storm_far_lod)
