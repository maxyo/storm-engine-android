STORM_SETUP(
    TARGET_NAME renderer
    TYPE storm_module
    DEPENDENCIES core util storm_bgfx
)

# Renderer-neutral resource contracts use bounded std::span views.
target_compile_features(renderer PUBLIC cxx_std_20)

# Renderer drives the bgfx shim's device directly (renderer_bgfx/src/shim): the two halves of one renderer.
target_include_directories(renderer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../renderer_bgfx/src")
