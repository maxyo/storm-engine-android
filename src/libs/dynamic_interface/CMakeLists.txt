# Caribbean Legend dynamic HUD: the "DynamicInterfaceService" entity (custom group registry) and
# the dynamic HUD data model for the portable UI (see src/dynamic_interface_service.hpp).
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
STORM_SETUP(
    TARGET_NAME dynamic_interface
    TYPE storm_module
    DEPENDENCIES core nlohmann_json::nlohmann_json
)

# Host check through the real core (checks/dynamic_interface_check.cpp), built on request:
#   ninja -C <build> dynamic_interface_check && <build>/dynamic_interface_check
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(dynamic_interface_check EXCLUDE_FROM_ALL checks/dynamic_interface_check.cpp)
    target_link_libraries(dynamic_interface_check PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,dynamic_interface>" core zlib)
    target_include_directories(dynamic_interface_check PRIVATE src)
endif()
