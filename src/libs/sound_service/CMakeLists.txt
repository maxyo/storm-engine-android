if(STORM_OPEN_AUDIO)
    set(STORM_AUDIO_DEP storm_open_audio)
else()
    set(STORM_AUDIO_DEP fmod)
endif()
STORM_SETUP(
    TARGET_NAME sound_service
    TYPE storm_module
    DEPENDENCIES core ${STORM_AUDIO_DEP} renderer
)
if(STORM_OPEN_AUDIO)
    target_compile_definitions(sound_service PUBLIC STORM_OPEN_AUDIO=1)
endif()
