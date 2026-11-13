#include "effects/shadows.hpp"

#include "shim/device.hpp"
#include "storm/render/frame_owner.hpp"

#include <bx/math.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_map>

namespace storm::effects
{

namespace
{
// Shadowed region: a sphere of this radius whose centre lies this far in front of the camera (the third-person
// cameras look at the player from a few metres behind), casters up to kCasterReach towards the sun (masts, palms).
// 30 m (was 40, the owner's choice from the before/after): a quarter fewer town tiles, a quarter finer texels.
constexpr float kRadius = 30.0f;
constexpr float kAhead = 18.0f;
constexpr float kCasterReach = 120.0f;
constexpr bgfx::ViewId kShadowView = render::kPreGameViewBegin;
// The place's still geometry is cut into tiles of this size (xz) so the cascade draws only what it reaches.
constexpr float kTileSize = 24.0f;


float Dot(const std::array<float, 3> &a, const std::array<float, 3> &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> Normalize(std::array<float, 3> v)
{
    const float l = std::sqrt(Dot(v, v));
    if (l > 1e-9f)
        for (float &x : v)
            x /= l;
    return v;
}

// From `from` towards `to` by at most `max_radians` (on the great circle).

} // namespace

void SunShadows::set_casters(const render::EffectsLocationGeometry &geometry)
{
    casters_name_ = std::string(geometry.name);
    casters_.assign(geometry.positions.begin(), geometry.positions.end());
    caster_indices_.assign(geometry.indices.begin(), geometry.indices.end());
    caster_uvs_.clear();
    caster_texture_.assign(caster_indices_.size() / 3, nullptr);
    if (geometry.uvs.size() == geometry.positions.size())
    {
        caster_uvs_.assign(geometry.uvs.begin(), geometry.uvs.end());
        for (const auto &surface : geometry.surfaces)
        {
            const shim::TexStore *store = shim::store_of(surface.texture);
            if (!store || !store->cutout || location_caster_material(store).cut <= 0.0f ||
                surface.first_index + surface.index_count > caster_indices_.size())
                continue;
            for (uint32_t i = surface.first_index / 3; i < (surface.first_index + surface.index_count) / 3; ++i)
                caster_texture_[i] = surface.texture;
        }
    }
    else
        caster_uvs_.assign(casters_.size(), {0.0f, 0.0f});
    tiles_dirty_ = true;
}

bool SunShadows::prepare(shim::Device &device, const std::array<float, 16> &view, const std::array<float, 3> &origin,
                         Casters &casters)
{
    if (prepared_)
        return active_;
    prepared_ = true;
    active_ = false;
    if (!enabled() || !sun_present_)
        return false;
    // Camera position -t * R^T and forward (+z of the row-vector view: the third column), in the device's world.
    fx::ShadowFrame frame;
    frame.view = kShadowView;
    frame.sun_dir = {sun_dir_[0], sun_dir_[1], sun_dir_[2]};
    for (int i = 0; i < 3; ++i)
    {
        const float eye = -(view[12] * view[i * 4 + 0] + view[13] * view[i * 4 + 1] + view[14] * view[i * 4 + 2]);
        frame.focus[i] = eye + view[i * 4 + 2] * kAhead;
    }
    frame.radius = kRadius;
    frame.caster_reach = kCasterReach;
    active_ = shadows_.Begin(settings_, frame);
    if (!active_ || caster_indices_.empty())
        return active_;
    if (tiles_dirty_)
        build_tiles();
    // The place's still geometry the cascade reaches: tiles near the line through the focus along the sun, from
    // behind the focus up to the caster reach towards the sun. The tiles are absolute, the cascade in the device's world.
    const bgfx::ProgramHandle program = casters.program(device, Casters::Pass::Sun, false, CasterMaterial{});
    if (!bgfx::isValid(program))
        return active_;
    const std::array<float, 3> focus{frame.focus[0] + origin[0], frame.focus[1] + origin[1], frame.focus[2] + origin[2]};
    float to_device[16];
    bx::mtxTranslate(to_device, -origin[0], -origin[1], -origin[2]);
    for (const Tile &tile : tiles_)
    {
        std::array<float, 3> c, h;
        for (int k = 0; k < 3; ++k)
        {
            c[k] = 0.5f * (tile.lo[k] + tile.hi[k]) - focus[k];
            h[k] = 0.5f * (tile.hi[k] - tile.lo[k]);
        }
        const float tile_radius = std::sqrt(Dot(h, h));
        const std::array<float, 3> sun = Normalize(sun_dir_);
        const float along = Dot(c, sun);
        const std::array<float, 3> across{c[0] - along * sun[0], c[1] - along * sun[1], c[2] - along * sun[2]};
        if (along < -(kRadius + tile_radius) || along > kCasterReach + tile_radius ||
            Dot(across, across) > (kRadius + tile_radius) * (kRadius + tile_radius))
            continue;
        if (bgfx::isValid(tile.solid))
        {
            bgfx::setTransform(to_device);
            bgfx::setVertexBuffer(0, tiles_vb_);
            bgfx::setIndexBuffer(tile.solid);
            bgfx::setState(fx::Shadows::kCasterState);
            bgfx::submit(kShadowView, program);
        }
        // Foliage, nets, gratings: with their texture, so the shadow has their holes (the colour pass's rule).
        for (const auto &[texture, triangles] : tile.cut)
        {
            shim::TexStore *store = shim::store_of(texture);
            if (!store)
                continue;
            const bgfx::ProgramHandle program_cut = casters.program(device, Casters::Pass::Sun, false, location_caster_material(store));
            if (!bgfx::isValid(program_cut))
                continue;
            store->sync();
            bgfx::setTexture(0, casters.sampler(), store->handle);
            bgfx::setTransform(to_device);
            bgfx::setVertexBuffer(0, tiles_vb_);
            bgfx::setIndexBuffer(triangles);
            bgfx::setState(fx::Shadows::kCasterState);
            bgfx::submit(kShadowView, program_cut);
        }
        ++frame_tiles_;
        frame_tile_triangles_ += tile.triangles;
    }
    return active_;
}

void SunShadows::build_tiles()
{
    destroy_tiles();
    tiles_dirty_ = false;
    if (caster_indices_.empty())
        return;
    float x0 = 1e30f, z0 = 1e30f;
    for (const fx::Vec3 &p : casters_)
    {
        x0 = std::min(x0, p[0]);
        z0 = std::min(z0, p[2]);
    }
    // One vertex buffer of the whole place (positions, texture coordinates); per tile the solid triangles and, per
    // cut-out texture, its own.
    struct Cell
    {
        fx::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        std::vector<uint32_t> solid;
        std::vector<std::pair<shim::BaseTexture *, std::vector<uint32_t>>> cut;
    };
    std::unordered_map<uint64_t, Cell> cells;
    for (size_t t = 0; t + 2 < caster_indices_.size(); t += 3)
    {
        const fx::Vec3 &a = casters_[caster_indices_[t]], &b = casters_[caster_indices_[t + 1]], &c = casters_[caster_indices_[t + 2]];
        const auto ix = static_cast<uint64_t>(std::max(0.0f, ((a[0] + b[0] + c[0]) / 3.0f - x0) / kTileSize));
        const auto iz = static_cast<uint64_t>(std::max(0.0f, ((a[2] + b[2] + c[2]) / 3.0f - z0) / kTileSize));
        Cell &cell = cells[ix << 32 | iz];
        for (const fx::Vec3 *v : {&a, &b, &c})
            for (int d = 0; d < 3; ++d)
            {
                cell.lo[d] = std::min(cell.lo[d], (*v)[d]);
                cell.hi[d] = std::max(cell.hi[d], (*v)[d]);
            }
        shim::BaseTexture *texture = caster_texture_[t / 3];
        std::vector<uint32_t> *into = &cell.solid;
        if (texture)
        {
            auto it = std::find_if(cell.cut.begin(), cell.cut.end(), [&](const auto &e) { return e.first == texture; });
            if (it == cell.cut.end())
                it = cell.cut.insert(cell.cut.end(), {texture, {}});
            into = &it->second;
        }
        into->insert(into->end(), caster_indices_.begin() + static_cast<std::ptrdiff_t>(t),
                     caster_indices_.begin() + static_cast<std::ptrdiff_t>(t + 3));
    }
    struct Vertex
    {
        float x, y, z, u, v;
    };
    std::vector<Vertex> vertices(casters_.size());
    for (size_t i = 0; i < casters_.size(); ++i)
        vertices[i] = {casters_[i][0], casters_[i][1], casters_[i][2], caster_uvs_[i][0], caster_uvs_[i][1]};
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
    tiles_vb_ = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(Vertex))), layout);
    auto index_buffer = [](const std::vector<uint32_t> &indices) {
        return bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * 4)), BGFX_BUFFER_INDEX32);
    };
    size_t cut_triangles = 0;
    for (auto &[key, cell] : cells)
    {
        Tile tile;
        tile.lo = cell.lo;
        tile.hi = cell.hi;
        tile.triangles = static_cast<uint32_t>(cell.solid.size() / 3);
        if (!cell.solid.empty())
            tile.solid = index_buffer(cell.solid);
        for (const auto &[texture, indices] : cell.cut)
        {
            tile.cut.push_back({texture, index_buffer(indices)});
            tile.triangles += static_cast<uint32_t>(indices.size() / 3);
            cut_triangles += indices.size() / 3;
        }
        tiles_.push_back(std::move(tile));
    }
    shim::logf("effects: sun shadows: %s's still geometry (%zu triangles, %zu of them cut out by their texture) casts in "
               "%zu tiles of %.0f m",
               casters_name_.c_str(), caster_indices_.size() / 3, cut_triangles, tiles_.size(), kTileSize);
}

void SunShadows::destroy_tiles()
{
    for (Tile &tile : tiles_)
    {
        if (bgfx::isValid(tile.solid))
            bgfx::destroy(tile.solid);
        for (auto &[texture, triangles] : tile.cut)
            bgfx::destroy(triangles);
    }
    tiles_.clear();
    if (bgfx::isValid(tiles_vb_))
        bgfx::destroy(tiles_vb_);
    tiles_vb_ = BGFX_INVALID_HANDLE;
}

void SunShadows::submit_caster(shim::Device &device, const float *world, bool skinned, Casters &casters,
                               const CasterMaterial &material)
{
    const bgfx::ProgramHandle program = casters.program(device, Casters::Pass::Sun, skinned, material);
    // The receiver's scissor and stencil stay with the receiver.
    bgfx::setScissor(UINT16_MAX);
    bgfx::setStencil(BGFX_STENCIL_NONE);
    bgfx::setTransform(world);
    bgfx::setState(fx::Shadows::kCasterState);
    if (bgfx::isValid(program))
        bgfx::submit(kShadowView, program);
    else
        bgfx::discard();
}

void SunShadows::end_frame()
{
    prepared_ = false;
    active_ = false;
    last_static_ = {frame_tiles_, frame_tile_triangles_};
    frame_tiles_ = frame_tile_triangles_ = 0;
}

void SunShadows::shutdown()
{
    destroy_tiles();
    shadows_.Shutdown();
}

void SunShadows::on_device_lost()
{
    tiles_.clear(); // the handles went with the device
    tiles_vb_ = BGFX_INVALID_HANDLE;
    tiles_dirty_ = true;
    shadows_.OnDeviceLost();
}

} // namespace storm::effects
