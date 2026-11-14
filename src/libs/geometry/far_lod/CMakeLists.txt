# Far LODs of .gm models (README.md): clusters and simplified levels by meshoptimizer. No engine dependencies:
# tests/far_lod builds it alone.
include(FetchContent)
# meshoptimizer 1.3 (MIT, Arseny Kapoulkine). Licence: third_party/licenses/meshoptimizer-MIT.txt.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_MESHOPTIMIZER=/path/to/this/revision
storm_fetch_declare(storm_meshoptimizer
    URL https://codeload.github.com/zeux/meshoptimizer/tar.gz/9e1f07b159d3cb777f1c67ed31fc11fd117986f4
    URL_HASH SHA256=e753b45fbc410031bd69cb278180ae31fa0d5430d0c3e662e3a3b100467ce0b3
    STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_meshoptimizer)

# Only the parts we call.
set(meshopt "${storm_meshoptimizer_SOURCE_DIR}/src")
add_library(storm_meshoptimizer STATIC
    "${meshopt}/allocator.cpp"
    "${meshopt}/indexcodec.cpp"
    "${meshopt}/simplifier.cpp"
    "${meshopt}/spatialorder.cpp"
    "${meshopt}/vcacheoptimizer.cpp")
target_include_directories(storm_meshoptimizer SYSTEM PUBLIC "${meshopt}")
set_target_properties(storm_meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(storm_far_lod STATIC far_lod.cpp far_lod.hpp layout.cpp)
target_compile_features(storm_far_lod PUBLIC cxx_std_20)
target_include_directories(storm_far_lod PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(storm_far_lod PRIVATE storm_meshoptimizer)
set_target_properties(storm_far_lod PROPERTIES POSITION_INDEPENDENT_CODE ON)
