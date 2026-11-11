STORM_SETUP(
    TARGET_NAME core
    TYPE library
    DEPENDENCIES diagnostics math shared_headers fast_float ${SDL2_LIBRARIES} window
)
