if (WIN32)
set(SYSTEM_DEPS
    "ddraw"
    "amstrmid"
)
endif()

STORM_SETUP(
    TARGET_NAME xinterface
    TYPE storm_module
    DEPENDENCIES animation core geometry model renderer util bimg ${SYSTEM_DEPS}
    TEST_DEPENDENCIES catch2
)