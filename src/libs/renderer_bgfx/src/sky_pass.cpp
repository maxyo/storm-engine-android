#include "sky_pass.hpp"
namespace storm::render {
namespace {
struct Vertex {float x,y,z; uint8_t r,g,b,a;float u,v,u1,v1;};
static_assert(sizeof(Vertex)==32);
}
SkyPass::SkyPass(TextureStore& textures,const bgfx::Memory* vsBytes,const bgfx::Memory* fsBytes):textures_(textures) {
    layout_.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true)
        .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1,2,bgfx::AttribType::Float).end();
    auto vs=bgfx::createShader(vsBytes),fs=bgfx::createShader(fsBytes);
    if(!bgfx::isValid(vs)||!bgfx::isValid(fs)) {
        if(bgfx::isValid(vs)) bgfx::destroy(vs);
        if(bgfx::isValid(fs)) bgfx::destroy(fs);
        throw std::runtime_error("bgfx sky shader creation failed");
    }
    program_=bgfx::createProgram(vs,fs,true);
    texture0_=bgfx::createUniform("s_sky0",bgfx::UniformType::Sampler);
    texture1_=bgfx::createUniform("s_sky1",bgfx::UniformType::Sampler);
    parameters_=bgfx::createUniform("u_sky",bgfx::UniformType::Vec4);
    if(!bgfx::isValid(program_)||!bgfx::isValid(texture0_)||!bgfx::isValid(texture1_)||!bgfx::isValid(parameters_)) {
        if(bgfx::isValid(program_)) bgfx::destroy(program_);
        if(bgfx::isValid(texture0_)) bgfx::destroy(texture0_);
        if(bgfx::isValid(texture1_)) bgfx::destroy(texture1_);
        if(bgfx::isValid(parameters_)) bgfx::destroy(parameters_);
        throw std::runtime_error("bgfx sky material allocation failed");
    }
}
SkyPass::~SkyPass() {
    if(bgfx::isValid(vb_)) bgfx::destroy(vb_);
    if(bgfx::isValid(ib_)) bgfx::destroy(ib_);
    bgfx::destroy(program_);bgfx::destroy(texture0_);bgfx::destroy(texture1_);bgfx::destroy(parameters_);
}
void SkyPass::update(std::span<const TexturedVertex> input,std::span<const uint16_t> indices) {
    validateTexturedMesh(input,indices);
    std::vector<Vertex> vertices(input.size());
    for(size_t i=0;i<input.size();++i) {
        const auto& v=input[i];
        vertices[i]={v.x,v.y,v.z,uint8_t(v.argb>>16),uint8_t(v.argb>>8),uint8_t(v.argb),uint8_t(v.argb>>24),v.u,v.v,v.u1,v.v1};
    }
    auto vb=bgfx::createVertexBuffer(bgfx::copy(vertices.data(),uint32_t(vertices.size()*sizeof(Vertex))),layout_);
    auto ib=bgfx::createIndexBuffer(bgfx::copy(indices.data(),uint32_t(indices.size_bytes())));
    if(!bgfx::isValid(vb)||!bgfx::isValid(ib)) {
        if(bgfx::isValid(vb)) bgfx::destroy(vb);
        if(bgfx::isValid(ib)) bgfx::destroy(ib);
        throw std::runtime_error("bgfx sky mesh allocation failed");
    }
    if(bgfx::isValid(vb_)) bgfx::destroy(vb_);
    if(bgfx::isValid(ib_)) bgfx::destroy(ib_);
    vb_=vb;ib_=ib;indexCount_=indices.size();
}
void SkyPass::submit(const TexturedDraw& draw) {
    validateTexturedDraw(draw,indexCount_);
    const auto material=skyMaterial(draw.material);
    if(material==SkyMaterial::unsupported) throw std::invalid_argument("Sky material/depth variant not ported");
    if(!bgfx::isValid(vb_)||!bgfx::isValid(ib_)) throw std::logic_error("Sky mesh requires upload");
    // Resolve every binding before any submission, so a bad later face cannot
    // leave a partially drawn batch. Ordered sky/actors/alpha mask is essential.
    std::vector<std::array<bgfx::TextureHandle,2>> bindings;
    bindings.reserve(draw.ranges.size());
    for(const auto& range:draw.ranges) {
        const auto first=textures_.resolve(range.texture0);
        bindings.push_back({first,material==SkyMaterial::single?first:textures_.resolve(range.texture1)});
    }
    bgfx::setViewMode(draw.view,bgfx::ViewMode::Sequential);
    const float parameters[]{float(draw.textureFactor>>24)/255.f,float(material),0,0};
    uint64_t state=BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A;
    if(material==SkyMaterial::alphaMask)
        state|=BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_INV_SRC_ALPHA,BGFX_STATE_BLEND_SRC_ALPHA);
    for(size_t i=0;i<draw.ranges.size();++i) {
        const auto& range=draw.ranges[i];
        bgfx::setTransform(draw.world.data());bgfx::setVertexBuffer(0,vb_);
        bgfx::setIndexBuffer(ib_,range.firstIndex,range.indexCount);
        bgfx::setTexture(0,texture0_,bindings[i][0],BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
        bgfx::setTexture(1,texture1_,bindings[i][1],BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
        bgfx::setUniform(parameters_,parameters);bgfx::setState(state);bgfx::submit(draw.view,program_);
    }
}
}
