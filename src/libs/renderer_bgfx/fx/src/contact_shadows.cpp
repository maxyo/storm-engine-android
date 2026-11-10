#include "storm/fx/contact_shadows.hpp"

#include "gpu_util.hpp"

#include <algorithm>

namespace storm::fx
{

ContactShadows::~ContactShadows()
{
    Shutdown();
}

void ContactShadows::Shutdown()
{
    detail::Destroy(program_);
}

void ContactShadows::OnDeviceLost()
{
    program_ = BGFX_INVALID_HANDLE;
}

void ContactShadows::Add(Vec3 ground, float radius, float darkness)
{
    if (radius > 0.0f && darkness > 0.0f)
        blobs_.push_back({ground, radius, std::min(darkness, 1.0f)});
}

void ContactShadows::Submit(bgfx::ViewId view, const Settings &settings)
{
    std::vector<Blob> blobs;
    blobs.swap(blobs_);
    if (!settings.contact_shadows || blobs.empty())
        return;
    struct Vertex
    {
        float pos[3], color[3], uv[3];
    };
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
            .end();
        return l;
    }();
    const uint32_t count = std::min(static_cast<uint32_t>(blobs.size()) * 6, bgfx::getAvailTransientVertexBuffer(static_cast<uint32_t>(blobs.size()) * 6, layout)) / 6 * 6;
    if (count == 0)
        return;
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, count, layout);
    auto *v = reinterpret_cast<Vertex *>(buffer.data);
    for (uint32_t i = 0; i < count / 6; ++i)
    {
        const Blob &b = blobs[i];
        const float corners[6][2] = {{-1, -1}, {1, -1}, {-1, 1}, {-1, 1}, {1, -1}, {1, 1}};
        for (const auto &corner : corners)
            // Two centimetres above the floor, so the blob does not fight it for depth.
            *v++ = {{b.ground[0] + corner[0] * b.radius, b.ground[1] + 0.02f, b.ground[2] + corner[1] * b.radius},
                    {b.darkness, b.darkness, b.darkness},
                    {corner[0], corner[1], 3.0f}};
    }
    if (!bgfx::isValid(program_))
        program_ = detail::LoadProgram("vs_fx_sprite_world", "fs_fx_sprite");
    bgfx::setVertexBuffer(0, &buffer);
    // Multiplies the frame buffer: result = destination * (1 - darkness * falloff).
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO));
    bgfx::submit(view, program_);
}

} // namespace storm::fx
