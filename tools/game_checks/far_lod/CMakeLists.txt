# Far LOD game-data check (run.py): check.cpp over the game's models, with the far LOD library and the host check's
# verify.hpp (FAR_LOD_INCLUDES, from run.py).
cmake_minimum_required(VERSION 3.21)
project(storm_far_lod_game_check LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormBuildCache.cmake")
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../src/libs/geometry/far_lod" far_lod)
add_executable(far_lod_game_check check.cpp)
target_compile_options(far_lod_game_check PRIVATE -Wall -Wextra -Werror)
target_include_directories(far_lod_game_check PRIVATE ${FAR_LOD_INCLUDES})
target_link_libraries(far_lod_game_check PRIVATE storm_far_lod)
