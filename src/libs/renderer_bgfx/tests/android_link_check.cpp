#include "fog_pass.hpp"

// Link-only ABI probe; invoking it requires an initialized bgfx context.
extern "C" void storm_bgfx_link_probe(const bgfx::Memory* vs, const bgfx::Memory* fs,
    const storm::weather::FogMesh* mesh, const float* world) {
    storm::render::FogPass pass(vs,fs);
    pass.update(*mesh);
    pass.submit(0,world);
    bgfx::frame();
}
