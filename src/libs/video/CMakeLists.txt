# Video playback on FFmpeg (cmake/ffmpeg.cmake provides storm_ffmpeg). See README.md.
if(ANDROID)
    set(VIDEO_PLATFORM_DEPS sdl)
endif()
STORM_SETUP(
    TARGET_NAME video
    TYPE library
    DEPENDENCIES storm_ffmpeg ${VIDEO_PLATFORM_DEPS}
)
