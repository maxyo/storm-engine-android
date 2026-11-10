STORM_SETUP(
    TARGET_NAME sea_cameras
    TYPE storm_module
    DEPENDENCIES collide core geometry island model renderer sea sea_ai
)

# Host check of FIRE_CAMERA (checks/fire_camera_check.cpp), built on request:
#   ninja -C <build> fire_camera_check && <build>/fire_camera_check
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(fire_camera_check EXCLUDE_FROM_ALL checks/fire_camera_check.cpp)
    target_link_libraries(fire_camera_check PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,sea_cameras>" core zlib)
    target_include_directories(fire_camera_check PRIVATE src)
endif()
