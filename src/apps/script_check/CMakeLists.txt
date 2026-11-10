# Host tool: compiles a game's whole script tree (no renderer, no game start) and reports every
# compile error (README.md). Links only core: STORM_SETUP executables pull in every engine module.
if(ANDROID)
    return()
endif()

add_executable(script_check src/main.cpp)
target_link_libraries(script_check PRIVATE core diagnostics util window ${SDL2_LIBRARIES} zlib)
target_include_directories(script_check PRIVATE "${CMAKE_SOURCE_DIR}/src/libs/core/src")
