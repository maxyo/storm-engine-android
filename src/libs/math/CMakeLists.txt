include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/glm.cmake)

STORM_SETUP(
    TARGET_NAME math
    TYPE library
    DEPENDENCIES glm::glm-header-only
)
