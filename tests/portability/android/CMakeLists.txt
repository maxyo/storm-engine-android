cmake_minimum_required(VERSION 3.19)
project(storm-portable-math-check LANGUAGES CXX)
# This is a math check executable, NOT the engine or an Android app.
add_executable(storm-math-check ../smoke.cpp)
target_compile_features(storm-math-check PRIVATE cxx_std_17)
target_include_directories(storm-math-check PRIVATE
    ../../../src/libs/math/include ../../../src/libs/sea/src)
target_compile_options(storm-math-check PRIVATE -ffp-contract=off)
