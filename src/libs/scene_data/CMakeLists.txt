# Data-only parsers of the Caribbean Legend JSON presets (model sidecar materials, lighting presets,
# texture library, effects table). No rendering; the PBR renderer consumes these structures.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
add_library(scene_data STATIC
    src/json_reader.cpp
    src/model_material.cpp
    src/lighting_preset.cpp
    src/texture_library.cpp
    src/effects_table.cpp)
target_compile_features(scene_data PUBLIC cxx_std_20)
target_include_directories(scene_data PUBLIC include PRIVATE src)
target_link_libraries(scene_data PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(scene_data PROPERTIES POSITION_INDEPENDENT_CODE ON)
