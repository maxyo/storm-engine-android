STORM_SETUP(
    TARGET_NAME weather
    TYPE storm_module
    DEPENDENCIES collide core geometry renderer sea ship
)
if(STORM_ENABLE_BGFX)
    target_link_libraries(weather PRIVATE storm_bgfx_world)
    target_compile_definitions(weather PRIVATE STORM_ENABLE_BGFX=1)
endif()
