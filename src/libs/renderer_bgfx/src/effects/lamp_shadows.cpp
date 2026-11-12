#include "effects/lamp_shadows.hpp"

#include "shim/device.hpp"
#include "storm/render/frame_owner.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::effects
{

namespace
{
constexpr bgfx::ViewId kLampStaticView = render::kPreGameViewBegin + 1;
constexpr bgfx::ViewId kLampDynamicView = render::kPreGameViewBegin + 2;
constexpr uint64_t kCasterState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr uint64_t kMapFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP |
                               BGFX_SAMPLER_V_CLAMP;

// World -> clip of a face, mapped into its tile, for the backend's clip depth (GL: -1..1).
std::array<float, 16> face_matrix(const fx::Lamp &lamp, int slot, int face, int columns, int rows)
{
    std::array<float, 16> vp = fx::LampFaceViewProj(lamp.position, face, lamp.range);
    if (bgfx::getCaps()->homogeneousDepth)
        for (int r = 0; r < 4; ++r)
            vp[r * 4 + 2] = 2.0f * vp[r * 4 + 2] - vp[r * 4 + 3];
    const std::array<float, 16> tile = fx::TileClip(fx::LampAtlasTile(slot, face, columns, rows));
    std::array<float, 16> out{};
    bx::mtxMul(out.data(), vp.data(), tile.data());
    return out;
}

void scissor_tile(int slot, int face, int columns, int rows, int face_size)
{
    const int tile = slot * 6 + face;
    (void)rows;
    bgfx::setScissor(static_cast<uint16_t>((tile % columns) * face_size), static_cast<uint16_t>((tile / columns) * face_size),
                     static_cast<uint16_t>(face_size), static_cast<uint16_t>(face_size));
}
} // namespace

void LampShadows::configure(const fx::Settings &settings)
{
    const int slots = std::clamp<int>(settings.lamp_shadows, 0, fx::kMaxShadowLamps);
    const int face = slots > 2 ? 512 : 256;
    if (slots != slots_ || face != face_size_)
    {
        for (bgfx::FrameBufferHandle *fb : {&static_fb_, &dynamic_fb_})
            if (bgfx::isValid(*fb))
                bgfx::destroy(*fb); // owns its texture
        static_fb_ = dynamic_fb_ = BGFX_INVALID_HANDLE;
        static_map_ = dynamic_map_ = BGFX_INVALID_HANDLE;
        static_dirty_ = true;
    }
    slots_ = slots;
    face_size_ = face;
    dynamic_face_size_ = face / 2;
    columns_ = 6;
    rows_ = std::max(slots, 1);
}

void LampShadows::set_lamps(std::vector<fx::Lamp> lamps)
{
    // Candles of one candlestick: one shadow.
    lamps_ = fx::ClusterLamps(lamps, 0.6f);
}

void LampShadows::set_geometry(const std::vector<fx::Vec3> &positions, const std::vector<uint32_t> &indices)
{
    if (positions == positions_ && indices == indices_)
        return;
    positions_ = positions;
    indices_ = indices;
    geometry_dirty_ = true;
    static_dirty_ = true;
}

void LampShadows::find_shaders()
{
    if (fs_caster_)
        return;
    vs_caster_[0] = shim::shader_id_by_name("vs_ff_caster");
    vs_caster_[1] = shim::shader_id_by_name("vs_ff_caster_skin");
    fs_caster_ = shim::shader_id_by_name("fs_ff_caster");
}

bool LampShadows::prepare_programs(shim::Device &device, int &budget)
{
    if (!enabled())
        return true;
    find_shaders();
    return device.prepare_program(vs_caster_[0], fs_caster_, budget) && device.prepare_program(vs_caster_[1], fs_caster_, budget);
}

void LampShadows::ensure_targets()
{
    if (!bgfx::isValid(open_map_))
    {
        // 1x1 depth 1.0: every compare passes, the lamp is unshadowed.
        const uint16_t one = 0xffff;
        open_map_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D16, BGFX_SAMPLER_COMPARE_LEQUAL,
                                          bgfx::copy(&one, sizeof(one)));
        s_static_ = bgfx::createUniform("s_fxLampStatic", bgfx::UniformType::Sampler);
        s_dynamic_ = bgfx::createUniform("s_fxLampDynamic", bgfx::UniformType::Sampler);
        u_view_ = bgfx::createUniform("u_fxLampView", bgfx::UniformType::Mat4);
        u_lamps_ = bgfx::createUniform("u_fxLamps", bgfx::UniformType::Vec4, static_cast<uint16_t>(uniforms_.size()));
    }
    if (!enabled() || bgfx::isValid(static_fb_))
        return;
    static_map_ = bgfx::createTexture2D(static_cast<uint16_t>(columns_ * face_size_), static_cast<uint16_t>(rows_ * face_size_),
                                        false, 1, bgfx::TextureFormat::D16, kMapFlags);
    static_fb_ = bgfx::createFrameBuffer(1, &static_map_, true);
    dynamic_map_ = bgfx::createTexture2D(static_cast<uint16_t>(columns_ * dynamic_face_size_),
                                         static_cast<uint16_t>(rows_ * dynamic_face_size_), false, 1, bgfx::TextureFormat::D16,
                                         kMapFlags);
    dynamic_fb_ = bgfx::createFrameBuffer(1, &dynamic_map_, true);
    static_dirty_ = true;
}

bool LampShadows::prepare(shim::Device &device, const std::array<float, 3> &eye)
{
    if (prepared_)
        return active_;
    prepared_ = true;
    active_ = false;
    ensure_targets();
    if (!enabled() || lamps_.empty() || positions_.empty())
        return false;
    chosen_ = fx::SelectShadowLamps(lamps_, {eye[0], eye[1], eye[2]}, slots_, chosen_);
    if (chosen_.empty())
        return false;
    // A slot's static faces stay valid while its lamp stays where it was (flicker only changes the colour).
    std::vector<fx::Lamp> wanted;
    for (const int i : chosen_)
        wanted.push_back(lamps_[i]);
    bool moved = wanted.size() != slot_lamps_.size();
    for (size_t i = 0; !moved && i < wanted.size(); ++i)
        for (int k = 0; k < 3; ++k)
            moved |= std::fabs(wanted[i].position[k] - slot_lamps_[i].position[k]) > 0.01f ||
                     std::fabs(wanted[i].range - slot_lamps_[i].range) > 0.01f;
    slot_lamps_ = wanted;
    static_dirty_ |= moved;
    if (static_dirty_)
        draw_static(device);

    bgfx::setViewName(kLampDynamicView, "fx lamp shadows (characters)");
    bgfx::setViewMode(kLampDynamicView, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(kLampDynamicView, dynamic_fb_);
    bgfx::setViewRect(kLampDynamicView, 0, 0, static_cast<uint16_t>(columns_ * dynamic_face_size_),
                      static_cast<uint16_t>(rows_ * dynamic_face_size_));
    bgfx::setViewClear(kLampDynamicView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(kLampDynamicView, nullptr, nullptr);
    bgfx::touch(kLampDynamicView);

    // Receiver uniforms of the frame (u_fxLamps, ff_fragment.sh).
    uniforms_ = {};
    uniforms_[0] = {static_cast<float>(slot_lamps_.size()), bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f,
                    1.0f / static_cast<float>(rows_), 0.0f};
    for (size_t i = 0; i < slot_lamps_.size(); ++i)
    {
        // The colour of this frame (flicker), the faces of the slot.
        const fx::Lamp &l = lamps_[chosen_[i]];
        uniforms_[1 + 4 * i] = {l.position[0], l.position[1], l.position[2], slot_lamps_[i].range};
        uniforms_[2 + 4 * i] = {l.color[0], l.color[1], l.color[2], l.range};
        uniforms_[3 + 4 * i] = {l.att0, l.att1, l.att2, static_cast<float>(i)};
        uniforms_[4 + 4 * i] = {1.0f / static_cast<float>(columns_ * face_size_), 0.0f, 0.0f, 0.0f};
    }
    active_ = true;
    return true;
}

void LampShadows::draw_static(shim::Device &device)
{
    static_dirty_ = false;
    if (geometry_dirty_ || !bgfx::isValid(static_vb_))
    {
        if (bgfx::isValid(static_vb_))
            bgfx::destroy(static_vb_);
        if (bgfx::isValid(static_ib_))
            bgfx::destroy(static_ib_);
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        static_vb_ = bgfx::createVertexBuffer(
            bgfx::copy(positions_.data(), static_cast<uint32_t>(positions_.size() * sizeof(fx::Vec3))), layout);
        static_ib_ = bgfx::createIndexBuffer(
            bgfx::copy(indices_.data(), static_cast<uint32_t>(indices_.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);
        geometry_dirty_ = false;
    }
    find_shaders();
    const bgfx::ProgramHandle program = device.program(vs_caster_[0], fs_caster_); // logs a failure
    bgfx::setViewName(kLampStaticView, "fx lamp shadows (location)");
    bgfx::setViewMode(kLampStaticView, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(kLampStaticView, static_fb_);
    bgfx::setViewRect(kLampStaticView, 0, 0, static_cast<uint16_t>(columns_ * face_size_), static_cast<uint16_t>(rows_ * face_size_));
    bgfx::setViewClear(kLampStaticView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(kLampStaticView, nullptr, nullptr);
    bgfx::touch(kLampStaticView);
    if (!bgfx::isValid(program))
        return;
    for (size_t slot = 0; slot < slot_lamps_.size(); ++slot)
        for (int face = 0; face < 6; ++face)
        {
            const std::array<float, 16> m = face_matrix(slot_lamps_[slot], static_cast<int>(slot), face, columns_, rows_);
            scissor_tile(static_cast<int>(slot), face, columns_, rows_, face_size_);
            bgfx::setTransform(m.data());
            bgfx::setVertexBuffer(0, static_vb_);
            bgfx::setIndexBuffer(static_ib_);
            bgfx::setState(kCasterState);
            bgfx::submit(kLampStaticView, program);
        }
    if (!reported_)
        shim::logf("effects: lamp shadows: %zu lamps of %zu (after merging candles), faces %d px, characters %d px",
                   slot_lamps_.size(), lamps_.size(), face_size_, dynamic_face_size_);
    reported_ = true;
}

void LampShadows::submit_caster(shim::Device &device, const float *world, bool skinned, const std::array<float, 3> &center,
                                float radius)
{
    if (!active_)
        return;
    find_shaders();
    const bgfx::ProgramHandle program = device.program(vs_caster_[skinned ? 1 : 0], fs_caster_);
    if (!bgfx::isValid(program))
        return;
    for (size_t slot = 0; slot < slot_lamps_.size(); ++slot)
    {
        const fx::Lamp &l = slot_lamps_[slot];
        const float dx = center[0] - l.position[0], dy = center[1] - l.position[1], dz = center[2] - l.position[2];
        if (dx * dx + dy * dy + dz * dz > (l.range + radius) * (l.range + radius))
            continue;
        for (int face = 0; face < 6; ++face)
        {
            if (!fx::SphereInLampFace(l.position, face, {center[0], center[1], center[2]}, radius))
                continue;
            const std::array<float, 16> m = face_matrix(l, static_cast<int>(slot), face, columns_, rows_);
            std::array<float, 16> wm{};
            bx::mtxMul(wm.data(), world, m.data());
            scissor_tile(static_cast<int>(slot), face, columns_, rows_, dynamic_face_size_);
            bgfx::setStencil(BGFX_STENCIL_NONE);
            bgfx::setTransform(wm.data());
            bgfx::setState(kCasterState);
            // The draw's buffers and bone palette stay for the next copy; Lighting::submit_caster discards at the end.
            bgfx::submit(kLampDynamicView, program, 0, BGFX_DISCARD_NONE);
        }
    }
}

void LampShadows::bind(bool active, const std::array<float, 16> &view)
{
    ensure_targets();
    const bool on = active && active_;
    // The view -> world matrix serves the AO volume's normals too: set for every draw.
    if (view != bound_view_)
    {
        bx::mtxInverse(view_to_world_.data(), view.data());
        bound_view_ = view;
    }
    static const std::array<std::array<float, 4>, 1 + 4 * fx::kMaxShadowLamps> none{};
    bgfx::setUniform(u_view_, view_to_world_.data());
    bgfx::setUniform(u_lamps_, on ? uniforms_.data() : none.data(), static_cast<uint16_t>(uniforms_.size()));
    const uint32_t sampler = BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::setTexture(fx::kLampDynamicStage, s_dynamic_, on ? dynamic_map_ : open_map_, sampler);
    bgfx::setTexture(fx::kLampStaticStage, s_static_, on ? static_map_ : open_map_, sampler);
}

void LampShadows::end_frame()
{
    prepared_ = false;
    active_ = false;
}

void LampShadows::shutdown()
{
    for (bgfx::FrameBufferHandle *fb : {&static_fb_, &dynamic_fb_})
        if (bgfx::isValid(*fb))
            bgfx::destroy(*fb);
    if (bgfx::isValid(open_map_))
        bgfx::destroy(open_map_);
    if (bgfx::isValid(static_vb_))
        bgfx::destroy(static_vb_);
    if (bgfx::isValid(static_ib_))
        bgfx::destroy(static_ib_);
    for (bgfx::UniformHandle *u : {&s_static_, &s_dynamic_, &u_view_, &u_lamps_})
        if (bgfx::isValid(*u))
            bgfx::destroy(*u);
    on_device_lost();
}

void LampShadows::on_device_lost()
{
    static_fb_ = dynamic_fb_ = BGFX_INVALID_HANDLE;
    static_map_ = dynamic_map_ = open_map_ = BGFX_INVALID_HANDLE;
    static_vb_ = BGFX_INVALID_HANDLE;
    static_ib_ = BGFX_INVALID_HANDLE;
    s_static_ = s_dynamic_ = u_view_ = u_lamps_ = BGFX_INVALID_HANDLE;
    slot_lamps_.clear();
    static_dirty_ = geometry_dirty_ = true;
    bound_view_ = {};
}

} // namespace storm::effects
