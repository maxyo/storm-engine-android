include_guard(GLOBAL)
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# nlohmann/json 3.12.0, the engine's only JSON library (UI templates, CL data presets).
# Offline builds can set FETCHCONTENT_SOURCE_DIR_STORM_JSON to an extracted copy of this archive.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(storm_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_MakeAvailable(storm_json)
