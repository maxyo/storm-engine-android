#include "fog_pass.hpp"
#include <array>
#include <stdexcept>

namespace storm::render {
namespace {
struct Vertex { float x, y, z; uint8_t r, g, b, a; };
static_assert(sizeof(Vertex) == 16);
}
FogPass::FogPass(const bgfx::Memory* vertexShader, const bgfx::Memory* fragmentShader) {
    layout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    const auto vs = bgfx::createShader(vertexShader);
    const auto fs = bgfx::createShader(fragmentShader);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        throw std::runtime_error("bgfx fog shader creation failed");
    }
    program_ = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(program_)) throw std::runtime_error("bgfx fog program creation failed");
}
FogPass::~FogPass() {
    if (bgfx::isValid(vertices_)) bgfx::destroy(vertices_);
    if (bgfx::isValid(indices_)) bgfx::destroy(indices_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
}
void FogPass::update(std::span<const ColorVertex> input, std::span<const uint16_t> indices) {
    validateColorMesh(input,indices.empty()?std::span<const uint16_t>(topology_):indices);
    if(indices.empty()&&topology_.empty()) throw std::invalid_argument("First mesh update needs topology");
    std::vector<uint16_t> newTopology(indices.begin(),indices.end());
    std::vector<Vertex> vertices(input.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = input[i];
        vertices[i] = {v.x, v.y, v.z, uint8_t(v.argb >> 16), uint8_t(v.argb >> 8),
                       uint8_t(v.argb), uint8_t(v.argb >> 24)};
    }
    const bool replaceVertices=!bgfx::isValid(vertices_)||vertexCount_!=input.size();
    auto vb=vertices_;
    auto ib=indices_;
    if(replaceVertices) vb=bgfx::createDynamicVertexBuffer(uint32_t(input.size()),layout_);
    if(!indices.empty()) ib=bgfx::createIndexBuffer(bgfx::copy(indices.data(),uint32_t(indices.size_bytes())));
    if(!bgfx::isValid(vb)||!bgfx::isValid(ib)) {
        if(replaceVertices&&bgfx::isValid(vb)) bgfx::destroy(vb);
        if(!indices.empty()&&bgfx::isValid(ib)) bgfx::destroy(ib);
        throw std::runtime_error("bgfx colored mesh buffer allocation failed");
    }
    if(replaceVertices&&bgfx::isValid(vertices_)) bgfx::destroy(vertices_);
    if(!indices.empty()&&bgfx::isValid(indices_)) bgfx::destroy(indices_);
    vertices_=vb; indices_=ib; vertexCount_=uint32_t(input.size());
    if(!indices.empty()) topology_=std::move(newTopology);
    bgfx::update(vertices_,0,bgfx::copy(vertices.data(),uint32_t(vertices.size()*sizeof(Vertex))));
}
void FogPass::submit(const ColorDraw& draw) {
    validateColorDraw(draw);
    if(!supportsMaterial(draw.material)) throw std::invalid_argument("bgfx colored mesh material is not ported");
    if(!bgfx::isValid(vertices_)||!bgfx::isValid(indices_)) throw std::logic_error("fog pass requires update before submit");
    bgfx::setTransform(draw.world.data());
    bgfx::setVertexBuffer(0,vertices_);
    bgfx::setIndexBuffer(indices_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(draw.view,program_);
}
void FogPass::submit(bgfx::ViewId view, const float* world, bool enabled) {
    if (enabled) submit({std::span<const float,16>(world,16),"SkyFog",view});
}
}
