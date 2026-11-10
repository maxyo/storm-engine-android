# Caribbean Legend "snow" entity: snow / dust flakes around the camera (src/snow.hpp).
STORM_SETUP(
    TARGET_NAME snow
    TYPE storm_module
    DEPENDENCIES core math renderer
)

# Host check (checks/snow_check.cpp), built on request:
#   ninja -C <build> snow_check && <build>/snow_check
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(snow_check EXCLUDE_FROM_ALL checks/snow_check.cpp)
    target_link_libraries(snow_check PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,snow>" core renderer zlib)
    target_include_directories(snow_check PRIVATE src)
endif()
