STORM_SETUP(
    TARGET_NAME diagnostics
    TYPE library
    DEPENDENCIES core util spdlog $<$<BOOL:${STORM_ENABLE_CRASH_REPORTS}>:sentry-native>
)