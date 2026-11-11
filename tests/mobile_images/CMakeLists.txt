# Host check of the mobile picture registry and model JSON (production sources). Configured by
# run.py next to this file.
cmake_minimum_required(VERSION 3.21)
project(storm_mobile_images_check LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
option(STORM_SANITIZE "Build the check with ASan/UBSan" ON)
set(src "${CMAKE_CURRENT_LIST_DIR}/../../src/libs/xinterface/src")
add_executable(mobile_images_check check.cpp "${src}/mobile_image_cache.cpp")
target_compile_features(mobile_images_check PRIVATE cxx_std_20)
target_include_directories(mobile_images_check PRIVATE "${src}")
# The check asserts: keep assert() active in every build type.
target_compile_options(mobile_images_check PRIVATE -UNDEBUG -O1 -g -fno-omit-frame-pointer)
if(STORM_SANITIZE)
    target_compile_options(mobile_images_check PRIVATE -fsanitize=address,undefined)
    target_link_options(mobile_images_check PRIVATE -fsanitize=address,undefined)
endif()
