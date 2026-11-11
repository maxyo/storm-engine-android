# The game's portable UI host (Android): storm::ui::Host inside the engine process, drawn by
# the bgfx backend in the frame owner's UI views. See README.md.
if(NOT ANDROID)
    return()
endif()
STORM_SETUP(
    TARGET_NAME ui_host
    TYPE storm_module
    DEPENDENCIES storm_ui storm_ui_bgfx util sdl android log
)
