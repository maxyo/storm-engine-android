# Host check of the scene_data JSON parsers. Configured by run.py next to this file.
cmake_minimum_required(VERSION 3.21)
project(storm_scene_data_check LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../src/libs/scene_data" scene_data)
add_executable(scene_data_check check.cpp)
target_link_libraries(scene_data_check PRIVATE scene_data)
