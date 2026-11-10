# Caribbean Legend Effekseer effects: the "Effects" entity and the EFFECTS_LIBRARY script library
# (see README.md). Effects play through storm/render/effekseer.hpp (renderer_bgfx).
STORM_SETUP(
    TARGET_NAME effects
    TYPE storm_module
    DEPENDENCIES core math renderer scene_data util
)

# Host check: creates the entity through the core and drives it with script messages and the
# natives (checks/effects_check.cpp). Not part of the engine build; built and run on request:
#   ninja -C <build> effects_check && <build>/effects_check [<CL game dir>]
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(effects_check EXCLUDE_FROM_ALL checks/effects_check.cpp)
    # The entity and library register themselves from static objects: keep them all.
    target_link_libraries(effects_check PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,effects>" core renderer bgfx zlib)
    target_include_directories(effects_check PRIVATE src ../core/src)
endif()
