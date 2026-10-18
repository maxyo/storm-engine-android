#include "fog_pass.hpp"

// Link-only ABI probe; invoking it requires an initialized bgfx context.
extern "C" void storm_bgfx_link_probe(const bgfx::Memory* vs, const bgfx::Memory* fs,
    const storm::weather::FogMesh* mesh, const float* world) {
    storm::render::FogPass pass(vs,fs);
    pass.update(*mesh);
    pass.submit(0,world);
    bgfx::frame();
}

#ifdef STORM_BGFX_LINK_PREVIEW
#include "fog_preview.hpp"
extern "C" int storm_bgfx_preview_link_probe(const storm::render::FogPreviewOptions* options) {
    return storm::render::runFogPreview(*options);
}
#endif

#include "rgba_texture.hpp"
extern "C" bool storm_bgfx_texture_link_probe(const storm::render::TexturePixels* pixels,uint32_t* color) {
    storm::render::RgbaTexture texture(*pixels);
    return texture.sample(0.f,0.f,*color);
}

#include "sky_pass.hpp"
extern "C" void storm_bgfx_sky_link_probe(storm::render::TextureStore* textures,const bgfx::Memory* vs,
    const bgfx::Memory* fs,const storm::render::TexturedVertex* vertices,size_t vertexCount,
    const uint16_t* indices,size_t indexCount,const storm::render::TexturedDraw* draw) {
    storm::render::SkyPass pass(*textures,vs,fs);
    pass.update({vertices,vertexCount},{indices,indexCount});
    pass.submit(*draw);
}

#include "dynamic_vertex_stream_bgfx.hpp"
extern "C" void storm_bgfx_dynamic_stream_link_probe(const std::byte* bytes,size_t size) {
    storm::render::BgfxDynamicVertexStream stream;
    stream.upload({bytes,size});
    stream.bind();
}
