STORM_SETUP(
    TARGET_NAME sound_service
    TYPE storm_module
    DEPENDENCIES core storm_open_audio renderer
)
target_compile_definitions(sound_service PUBLIC STORM_OPEN_AUDIO=1)
