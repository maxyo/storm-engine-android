if (WIN32)
set(SYSTEM_DEPS "legacy_stdio_definitions")
else()
set(SYSTEM_DEPS storm_bgfx)
endif()

STORM_SETUP(
    TARGET_NAME renderer
    TYPE storm_module
    DEPENDENCIES core $<$<BOOL:${WIN32}>:directx> util ${SYSTEM_DEPS}
)

# Renderer-neutral resource contracts use bounded std::span views.
target_compile_features(renderer PUBLIC cxx_std_20)
