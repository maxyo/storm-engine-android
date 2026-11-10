STORM_SETUP(
    TARGET_NAME renderer
    TYPE storm_module
    DEPENDENCIES core util storm_bgfx
)

# Renderer-neutral resource contracts use bounded std::span views.
target_compile_features(renderer PUBLIC cxx_std_20)
