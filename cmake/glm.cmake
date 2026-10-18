include_guard(GLOBAL)
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# GLM 1.0.3. Both immutable revision and archive digest are checked in.
# FetchContent stores downloads under this build tree; offline builds can set
# FETCHCONTENT_SOURCE_DIR_STORM_GLM to a pre-extracted copy of this revision.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Header-only GLM" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "Do not build upstream GLM tests" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Do not install bundled GLM" FORCE)
FetchContent_Declare(storm_glm
    URL https://codeload.github.com/g-truc/glm/tar.gz/8d1fd52e5ab5590e2c81768ace50c72bae28f2ed
    URL_HASH SHA256=36eefbae41503e6822e8d9e1b4cc85c6a6621f17676f6258aaf0a64c2de0a09c
)
FetchContent_MakeAvailable(storm_glm)
