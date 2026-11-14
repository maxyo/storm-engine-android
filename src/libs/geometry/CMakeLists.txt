add_subdirectory(far_lod)

STORM_SETUP(
    TARGET_NAME geometry
    TYPE storm_module
    DEPENDENCIES core renderer storm_far_lod
)