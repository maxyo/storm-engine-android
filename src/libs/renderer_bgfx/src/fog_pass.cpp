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
void FogPass::update(const weather::FogMesh& mesh) {
    std::array<Vertex, weather::fogVertexCount> vertices;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = mesh.vertices[i];
        vertices[i] = {v.x, v.y, v.z, uint8_t(v.argb >> 16), uint8_t(v.argb >> 8),
                       uint8_t(v.argb), uint8_t(v.argb >> 24)};
    }
    if (!bgfx::isValid(vertices_)) vertices_ = bgfx::createDynamicVertexBuffer(weather::fogVertexCount, layout_);
    if (!bgfx::isValid(indices_)) indices_ = bgfx::createIndexBuffer(bgfx::copy(mesh.indices.data(), uint32_t(sizeof(mesh.indices))));
    if (!bgfx::isValid(vertices_) || !bgfx::isValid(indices_)) throw std::runtime_error("bgfx fog buffer allocation failed");
    bgfx::update(vertices_, 0, bgfx::copy(vertices.data(), uint32_t(sizeof(vertices))));
}
void FogPass::submit(bgfx::ViewId view, const float* world, bool enabled) {
    if (!enabled) return;
    if (!bgfx::isValid(vertices_) || !bgfx::isValid(indices_)) throw std::logic_error("fog pass requires update before submit");
    bgfx::setTransform(world);
    bgfx::setVertexBuffer(0, vertices_);
    bgfx::setIndexBuffer(indices_);
    // Sky.fx: no depth, lighting or cull; diffuse RGBA; src-alpha blending.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(view, program_);
}
}
