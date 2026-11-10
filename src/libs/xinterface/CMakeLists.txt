STORM_SETUP(
    TARGET_NAME xinterface
    TYPE storm_module
    DEPENDENCIES animation core geometry model renderer sound_service util video bimg
    TEST_DEPENDENCIES catch2
)