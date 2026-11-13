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
// Candles of one candlestick or sconce: one lamp, one shadow.
constexpr float kFixtureRadius = 0.5f;
// A lamp lights as far as its light on a baked surface stays above this share of white, at most kLampReach metres.
constexpr float kLampFloor = 0.06f;
constexpr float kLampReach = 12.0f;
// The lamps' strength on baked surfaces at Settings::lamp_gain 1: half of the D3D light the lit models take from the
// same lamp (the look the owner chose on the v3 sheets; the game's bakes hold little lamp light of their own).
constexpr float kBakedGain = 0.5f;
// Lamps whose light begins farther from the camera than this take no slot.
constexpr float kLampSight = 40.0f;
// Slots whose static faces are drawn per frame once a location is running (six faces each).
constexpr int kSlotsPerFrame = 2;
// Seconds a lamp takes to fade in after its faces are drawn, and to fade out before its slot goes to another lamp.
constexpr float kFadeSeconds = 0.3f;
// A carried lantern is in its carrier's hand: what is nearer to it than this (the lantern's own model, the hand and
// forearm) casts nothing. The carrier's body does: the dark side of a lantern is behind whoever holds it.
constexpr float kLanternHolder = 0.3f;
// A moving light's still faces are drawn again once it has moved this far.
constexpr float kMovingStep = 0.02f;
// Cells of the triangle grids, metres.
constexpr float kTriangleCell = 4.0f;
// Cells of the room light, metres: across, and a storey high.
constexpr float kRoomCell = 1.5f;
constexpr float kRoomStorey = 3.0f;

float distance(const fx::Vec3 &a, const fx::Vec3 &b)
{
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// World -> clip of a face, mapped into its tile, for the backend's clip depth (GL: -1..1). The caster's fragment
// shader replaces the depth by the distance to the lamp, so only the clipping uses this depth.
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

void scissor_tile(int slot, int face, int columns, int face_size)
{
    const int tile = slot * 6 + face;
    bgfx::setScissor(static_cast<uint16_t>((tile % columns) * face_size), static_cast<uint16_t>((tile / columns) * face_size),
                     static_cast<uint16_t>(face_size), static_cast<uint16_t>(face_size));
}
} // namespace

void LampShadows::configure(const fx::Settings &settings)
{
    int slots = std::clamp<int>(settings.lamp_shadows, 0, fx::kMaxShadowLamps);
    const int face = 256;
    // One row of six faces per slot.
    const int max_rows = static_cast<int>(bgfx::getCaps()->limits.maxTextureSize) / face;
    if (slots > max_rows)
    {
        shim::logf("effects: lamps: the device's largest texture holds %d lamp slots of the %d asked for", max_rows, slots);
        slots = max_rows;
    }
    const int moving_slots = std::min<int>(settings.lamp_moving, slots);
    if (slots != slots_ || face != face_size_ || moving_slots != moving_slots_)
    {
        for (bgfx::FrameBufferHandle *fb : {&static_fb_, &dynamic_fb_})
            if (bgfx::isValid(*fb))
                bgfx::destroy(*fb); // owns its texture
        static_fb_ = dynamic_fb_ = BGFX_INVALID_HANDLE;
        static_map_ = dynamic_map_ = BGFX_INVALID_HANDLE;
        for (Slot &slot : slot_)
            free_slot(slot);
        slot_.assign(static_cast<size_t>(slots), Slot{});
        for (int s = slots - moving_slots; s < slots; ++s)
            slot_[s].moving = true; // the last slots are kept for the lights that move
        layout_dirty_ = true;
    }
    moving_slots_ = moving_slots;
    if (settings.lamp_gain != gain_)
        layout_dirty_ = true; // the radii follow the gain
    slots_ = slots;
    face_size_ = face;
    dynamic_face_size_ = face / 2;
    columns_ = 6;
    rows_ = std::max(slots, 1);
    gain_ = settings.lamp_gain;
    baked_ = settings.lamp_baked;
    soft_ = settings.lamp_soft;
    model_lift_ = settings.lamp_model_lift;
    tint_ = settings.lamp_tint;
    debug_ = settings.lamp_debug;
}

void LampShadows::set_lamps(const std::vector<fx::Lamp> &lamps)
{
    raw_ = lamps;
    // Lights without light of their own (the lighter tool's negative "inside" lights, switched-off types) are no lamps.
    std::vector<fx::Lamp> shining;
    for (const fx::Lamp &lamp : raw_)
        if (0.299f * lamp.color[0] + 0.587f * lamp.color[1] + 0.114f * lamp.color[2] > 1e-3f)
            shining.push_back(lamp);
    std::vector<fx::Lamp> merged = fx::ClusterLamps(shining, kFixtureRadius);
    // A fixture's merged position is weighted by its flames' brightness and wanders a little with their flicker: the
    // lamps count as moved (new radii, slots and faces) only beyond that.
    bool moved = merged.size() != placed_.size();
    for (size_t i = 0; !moved && i < merged.size(); ++i)
        moved = distance(merged[i].position, placed_[i]) > 0.1f;
    if (moved || layout_dirty_)
    {
        radius_.resize(merged.size());
        placed_.resize(merged.size());
        for (size_t i = 0; i < merged.size(); ++i)
        {
            radius_[i] = fx::LampRadius(merged[i], kLampFloor / std::max(gain_ * kBakedGain, 1e-3f), kLampReach);
            placed_[i] = merged[i].position;
        }
        layout_dirty_ = true;
        // The radii follow the flicker while the layout is dirty: only a move or another gain is a new layout.
        if (moved || gain_ != layout_gain_)
            ++layout_;
        layout_gain_ = gain_;
    }
    lamps_ = std::move(merged);
    for (size_t i = 0; i < lamps_.size(); ++i)
        lamps_[i].range = radius_[i];
    raw_lamp_.assign(raw_.size(), -1);
    // A fixture is its flames, arms and glass, not a point: its own geometry casts nothing (the real flames fill each
    // other's shadows of it in), and nothing is lit from nearer than its size. The size reaches past its farthest flame.
    holder_.assign(lamps_.size(), fx::kLampHolderRadius);
    for (size_t j = 0; j < raw_.size(); ++j)
    {
        float best = kFixtureRadius * 1.01f;
        for (size_t i = 0; i < lamps_.size(); ++i)
            if (const float d = distance(raw_[j].position, lamps_[i].position); d < best)
            {
                best = d;
                raw_lamp_[j] = static_cast<int>(i);
            }
        if (raw_lamp_[j] >= 0)
            holder_[raw_lamp_[j]] = std::max(holder_[raw_lamp_[j]], best + fx::kLampHolderRadius);
    }
}

void LampShadows::set_geometry(const render::EffectsLocationGeometry &geometry)
{
    const auto same = [](const auto &kept, const auto &given) {
        return kept.size() == given.size() && std::equal(kept.begin(), kept.end(), given.begin());
    };
    const auto same_surfaces = [&] {
        size_t cut_indices = 0;
        for (const CutOut &cut : cut_)
            cut_indices += cut.indices.size();
        size_t given = 0;
        for (const auto &surface : geometry.surfaces)
            if (const shim::TexStore *store = shim::store_of(surface.texture); store && store->cutout)
                given += surface.index_count;
        return cut_indices == given;
    };
    if (same(positions_, geometry.positions) && same(indices_, geometry.indices) && same(colors_, geometry.colors) &&
        same(uvs_, geometry.uvs) && same_surfaces() && geometry.interior == interior_ && geometry.room_lo == room_lo_ &&
        geometry.room_hi == room_hi_)
        return;
    for (Slot &slot : slot_)
        free_slot(slot); // their index buffers and textures belong to the geometry that goes
    positions_.assign(geometry.positions.begin(), geometry.positions.end());
    colors_.assign(geometry.colors.begin(), geometry.colors.end());
    uvs_.assign(geometry.uvs.begin(), geometry.uvs.end());
    indices_.assign(geometry.indices.begin(), geometry.indices.end());
    interior_ = geometry.interior;
    room_lo_ = geometry.room_lo;
    room_hi_ = geometry.room_hi;
    // Triangles of a texture whose alpha cuts holes cast through it; all others cast whole.
    solid_.clear();
    cut_.clear();
    std::vector<bool> cut_index(indices_.size(), false);
    if (uvs_.size() == positions_.size())
        for (const auto &surface : geometry.surfaces)
        {
            const shim::TexStore *store = shim::store_of(surface.texture);
            if (!store || !store->cutout || surface.first_index + surface.index_count > indices_.size())
                continue;
            auto into = std::find_if(cut_.begin(), cut_.end(), [&](const CutOut &c) { return c.texture == surface.texture; });
            if (into == cut_.end())
                into = cut_.insert(cut_.end(), CutOut{surface.texture, {}});
            for (uint32_t i = surface.first_index; i < surface.first_index + surface.index_count; ++i)
            {
                into->indices.push_back(indices_[i]);
                cut_index[i] = true;
            }
        }
    else if (!geometry.surfaces.empty())
        shim::logf("ERROR: effects: lamps: the location's geometry came without texture coordinates: foliage, nets and "
                   "gratings cast whole shadows");
    for (size_t i = 0; i < indices_.size(); ++i)
        if (!cut_index[i])
            solid_.push_back(indices_[i]);
    // By where they are: a lamp that takes a slot, and a carried lantern every time it moves, ask for the ones around it.
    solid_grid_ = fx::BuildTriangleGrid(positions_, solid_, kTriangleCell);
    for (CutOut &cut : cut_)
        cut.grid = fx::BuildTriangleGrid(positions_, cut.indices, kTriangleCell);
    geometry_lo_ = geometry_hi_ = positions_.empty() ? fx::Vec3{} : positions_.front();
    for (const fx::Vec3 &p : positions_)
        for (int k = 0; k < 3; ++k)
        {
            geometry_lo_[k] = std::min(geometry_lo_[k], p[k]);
            geometry_hi_[k] = std::max(geometry_hi_[k], p[k]);
        }
    // The room light: interiors only (outdoors the weather's sun and ambient are the models' light).
    room_ = {};
    room_ready_ = false;
    const bool room_box = room_hi_[0] > room_lo_[0] && room_hi_[1] > room_lo_[1] && room_hi_[2] > room_lo_[2];
    if (interior_ && !positions_.empty())
    {
        if (colors_.size() == positions_.size() && room_box)
            room_ = fx::BuildRoomLight(positions_, colors_, indices_, room_lo_, room_hi_, kRoomCell, kRoomStorey, 48, 12);
        if (room_.rgba.empty())
            shim::logf("ERROR: effects: lamps: no room light for this interior (%s): its lit models keep the weather's ambient",
                       colors_.size() != positions_.size() ? "its geometry came without baked colours"
                       : !room_box                           ? "no room box"
                                                             : "no surface inside the room box");
    }
    room_dirty_ = true;
    geometry_dirty_ = true;
    layout_dirty_ = true;
    reported_ = false;
}

void LampShadows::ensure_targets()
{
    if (!bgfx::isValid(open_map_))
    {
        // 1x1 depth 1.0: every compare passes; a grid without lamps (also the stand-in of the room light).
        const uint16_t one = 0xffff;
        open_map_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D16, BGFX_SAMPLER_COMPARE_LEQUAL,
                                          bgfx::copy(&one, sizeof(one)));
        const uint8_t none[8] = {};
        open_grid_ = bgfx::createTexture3D(2, 1, 1, false, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                           bgfx::copy(none, sizeof(none)));
        s_static_ = bgfx::createUniform("s_fxLampStatic", bgfx::UniformType::Sampler);
        s_dynamic_ = bgfx::createUniform("s_fxLampDynamic", bgfx::UniformType::Sampler);
        s_grid_ = bgfx::createUniform("s_fxLampGrid", bgfx::UniformType::Sampler);
        s_room_ = bgfx::createUniform("s_fxRoomLight", bgfx::UniformType::Sampler);
        u_view_ = bgfx::createUniform("u_fxLampView", bgfx::UniformType::Mat4);
        u_lamps_ = bgfx::createUniform("u_fxLamps", bgfx::UniformType::Vec4, static_cast<uint16_t>(uniforms_.size()));
        u_grid_ = bgfx::createUniform("u_fxLampGrid", bgfx::UniformType::Vec4, static_cast<uint16_t>(grid_uniforms_.size()));
        u_caster_ = bgfx::createUniform("u_fxLampCaster", bgfx::UniformType::Vec4);
        // Two triangles over the whole clip square: a slot's row is cleared with them (the atlas is never cleared whole).
        static const float square[18] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0};
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        clear_vb_ = bgfx::createVertexBuffer(bgfx::makeRef(square, sizeof(square)), layout);
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
    layout_dirty_ = true;
}

void LampShadows::free_slot(Slot &slot)
{
    if (bgfx::isValid(slot.triangles))
        bgfx::destroy(slot.triangles);
    for (const auto &cut : slot.cut)
        bgfx::destroy(cut.second);
    const bool moving = slot.moving;
    slot = Slot{};
    slot.moving = moving;
    grid_dirty_ = true;
}

void LampShadows::choose_slots(const std::array<float, 3> &eye)
{
    const int count = static_cast<int>(lamps_.size());
    const int slots = static_slots();
    if (layout_dirty_)
        for (Slot &slot : slot_)
            free_slot(slot);
    // The lamps whose light begins nearest to the camera. A lamp keeps its slot until it is two places behind the
    // last one that fits, so two lamps at the same distance do not trade a slot back and forth.
    std::vector<std::pair<float, int>> order;
    for (int i = 0; i < count; ++i)
        if (const float d = distance(lamps_[i].position, {eye[0], eye[1], eye[2]}) - lamps_[i].range; d < kLampSight)
            order.emplace_back(d, i);
    std::sort(order.begin(), order.end());
    std::vector<int> rank(static_cast<size_t>(count), count + slots);
    for (size_t r = 0; r < order.size(); ++r)
        rank[static_cast<size_t>(order[r].second)] = static_cast<int>(r);
    std::vector<bool> placed(static_cast<size_t>(count), false);
    int kept = 0;
    for (Slot &slot : slot_)
    {
        if (slot.moving || slot.lamp < 0)
            continue;
        if (!slot.leaving && rank[slot.lamp] < slots + 2)
        {
            placed[slot.lamp] = true;
            ++kept;
        }
        else
            slot.leaving = true; // fades out; the slot is free once it has
    }
    std::vector<uint32_t> near;
    for (size_t r = 0; r < order.size() && kept < slots; ++r)
    {
        const int lamp = order[r].second;
        if (placed[lamp])
            continue;
        const auto free = std::find_if(slot_.begin(), slot_.end(), [](const Slot &s) { return !s.moving && s.lamp < 0; });
        if (free == slot_.end())
            break; // the slots that are leaving are still fading
        free->lamp = lamp;
        free->at = lamps_[lamp];
        free->holder = holder_[lamp];
        const auto buffer = [&](const fx::TriangleGrid &grid, const std::vector<uint32_t> &triangles) {
            near.clear();
            fx::TrianglesInSphere(grid, triangles, free->at.position, free->at.range, near, seen_);
            return near.empty() ? bgfx::IndexBufferHandle BGFX_INVALID_HANDLE
                                : bgfx::createIndexBuffer(
                                      bgfx::copy(near.data(), static_cast<uint32_t>(near.size() * sizeof(uint32_t))),
                                      BGFX_BUFFER_INDEX32);
        };
        free->triangles = buffer(solid_grid_, solid_);
        for (size_t c = 0; c < cut_.size(); ++c)
            if (const bgfx::IndexBufferHandle cut = buffer(cut_[c].grid, cut_[c].indices); bgfx::isValid(cut))
                free->cut.emplace_back(c, cut);
        placed[lamp] = true;
        ++kept;
    }
}

void LampShadows::choose_moving(const std::array<float, 3> &eye)
{
    if (moving_slots_ == 0)
        return;
    // The lights that move, nearest to the camera first: carried lanterns and torches, then the ships' lanterns.
    struct Candidate
    {
        fx::Lamp lamp;
        const ShipLight *on_ship = nullptr;
        const render::EffectsShip *ship = nullptr;
    };
    std::vector<Candidate> lights;
    for (const fx::Lamp &l : moving_)
        lights.push_back({l});
    for (const ShipLight &l : ship_lights_)
    {
        const auto ship = std::find_if(ships_.begin(), ships_.end(), [&](const render::EffectsShip &s) { return s.id == l.ship; });
        if (ship != ships_.end() && l.ship != 0)
            lights.push_back({l.lamp, &l, &*ship});
        else if (!ship_reported_)
        {
            ship_reported_ = true;
            shim::logf("ERROR: effects: lamps: a ship's lantern was reported without its ship: it casts no shadows (logged once)");
        }
    }
    std::vector<std::pair<float, size_t>> order;
    for (size_t i = 0; i < lights.size(); ++i)
        if (lights[i].lamp.range > 0.0f && // (a D3D light of no range lights nothing)
            0.299f * lights[i].lamp.color[0] + 0.587f * lights[i].lamp.color[1] + 0.114f * lights[i].lamp.color[2] > 1e-3f)
            order.emplace_back(distance(lights[i].lamp.position, {eye[0], eye[1], eye[2]}), i);
    std::sort(order.begin(), order.end());
    if (order.size() > static_cast<size_t>(moving_slots_))
        order.resize(static_cast<size_t>(moving_slots_));
    // How far the light is from the one the slot holds, negative: another light. A ship's lantern is the same while
    // it is the same place of the same ship; a carried one while it is within a metre of where it was.
    const auto apart = [](const Slot &slot, const Candidate &c) {
        if (c.on_ship)
            return slot.ship == c.on_ship->ship && distance(slot.local, c.on_ship->local) < 0.05f ? 0.0f : -1.0f;
        const float d = distance(c.lamp.position, slot.at.position);
        return slot.ship == 0 && d < 1.0f ? d : -1.0f;
    };
    // A light keeps the slot it had; the others take free ones.
    std::vector<bool> taken(order.size(), false);
    for (int s = static_slots(); s < slots_; ++s)
    {
        Slot &slot = slot_[s];
        if (slot.lamp != kMovingLamp || slot.leaving)
            continue;
        size_t best = order.size();
        float best_apart = 0.0f;
        for (size_t i = 0; i < order.size(); ++i)
        {
            const float d = taken[i] ? -1.0f : apart(slot, lights[order[i].second]);
            if (d >= 0.0f && (best == order.size() || d < best_apart))
            {
                best = i;
                best_apart = d;
            }
        }
        if (best == order.size())
        {
            slot.leaving = true; // its light is gone or out-ranked: fades out where it last was
            continue;
        }
        taken[best] = true;
        const Candidate &light = lights[order[best].second];
        if (light.ship)
        {
            // The ship carries it: its faces stay, only where the ship is changes.
            slot.at.position = light.lamp.position;
            bx::mtxInverse(slot.to_local.data(), light.ship->world.data());
        }
        else
        {
            slot.redraw |= best_apart > kMovingStep;
            if (slot.redraw)
                slot.at.position = light.lamp.position;
        }
        slot.at.color = light.lamp.color; // this frame's flicker
    }
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (taken[i])
            continue;
        const auto free = std::find_if(slot_.begin() + static_slots(), slot_.end(), [](const Slot &s) { return s.lamp == -1; });
        if (free == slot_.end())
            break; // a slot that is leaving is still fading
        const Candidate &light = lights[order[i].second];
        free->lamp = kMovingLamp;
        free->at = light.lamp;
        // It stands for one of the game's point lights (render::SetEffectsDynamicLights / SetEffectsShipLights) and
        // reaches as far as that light: its D3D range is the far end of its faces (ff_fragment.sh ffLampAdd).
        free->at.range = light.lamp.range;
        free->holder = kLanternHolder;
        free->redraw = true;
        if (light.ship)
        {
            free->ship = light.on_ship->ship;
            free->local = light.on_ship->local;
            bx::mtxInverse(free->to_local.data(), light.ship->world.data());
        }
    }
    // The geometry of a ship none of whose lanterns holds a slot any more goes.
    std::erase_if(ship_geometry_, [&](ShipGeometry &geometry) {
        if (std::any_of(slot_.begin(), slot_.end(), [&](const Slot &s) { return s.lamp == kMovingLamp && s.ship == geometry.ship; }))
            return false;
        free_ship_geometry(geometry);
        return true;
    });
}

void LampShadows::free_ship_geometry(ShipGeometry &geometry)
{
    if (bgfx::isValid(geometry.vertices))
        bgfx::destroy(geometry.vertices);
    if (bgfx::isValid(geometry.solid))
        bgfx::destroy(geometry.solid);
    for (const auto &cut : geometry.cut)
        bgfx::destroy(cut.second);
    geometry = ShipGeometry{};
}

bool LampShadows::ship_geometry_wanted(uint64_t ship, uint64_t signature) const
{
    return std::any_of(slot_.begin(), slot_.end(),
                       [&](const Slot &s) { return s.lamp == kMovingLamp && !s.leaving && s.ship == ship; }) &&
           std::none_of(ship_geometry_.begin(), ship_geometry_.end(),
                        [&](const ShipGeometry &g) { return g.ship == ship && g.signature == signature; });
}

void LampShadows::set_ship_geometry(uint64_t ship, uint64_t signature, const render::EffectsShipGeometry &geometry)
{
    auto kept = std::find_if(ship_geometry_.begin(), ship_geometry_.end(), [&](const ShipGeometry &g) { return g.ship == ship; });
    if (kept == ship_geometry_.end())
        kept = ship_geometry_.insert(ship_geometry_.end(), ShipGeometry{});
    else
        free_ship_geometry(*kept);
    kept->ship = ship;
    kept->signature = signature;
    kept->triangles = geometry.indices.size() / 3;
    for (Slot &slot : slot_)
        if (slot.lamp == kMovingLamp && slot.ship == ship)
            slot.redraw = true;
    if (geometry.positions.empty() || geometry.indices.empty())
        return; // the ship said why; its lanterns' still faces stay open
    const bool textured = geometry.uvs.size() == geometry.positions.size();
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
    const bgfx::Memory *vertices = bgfx::alloc(static_cast<uint32_t>(geometry.positions.size() * 5 * sizeof(float)));
    auto *out = reinterpret_cast<float *>(vertices->data);
    for (size_t i = 0; i < geometry.positions.size(); ++i, out += 5)
    {
        memcpy(out, geometry.positions[i].data(), 3 * sizeof(float));
        out[3] = textured ? geometry.uvs[i][0] : 0.0f;
        out[4] = textured ? geometry.uvs[i][1] : 0.0f;
    }
    kept->vertices = bgfx::createVertexBuffer(vertices, layout);
    // Triangles of a texture whose alpha cuts holes cast through it; all others cast whole.
    std::vector<bool> cut_index(geometry.indices.size(), false);
    std::vector<std::pair<shim::BaseTexture *, std::vector<uint32_t>>> cut;
    if (textured)
        for (const auto &surface : geometry.surfaces)
        {
            const shim::TexStore *store = shim::store_of(surface.texture);
            if (!store || !store->cutout || surface.first_index + surface.index_count > geometry.indices.size())
                continue;
            auto into = std::find_if(cut.begin(), cut.end(), [&](const auto &c) { return c.first == surface.texture; });
            if (into == cut.end())
                into = cut.insert(cut.end(), {surface.texture, {}});
            for (uint32_t i = surface.first_index; i < surface.first_index + surface.index_count; ++i)
            {
                into->second.push_back(geometry.indices[i]);
                cut_index[i] = true;
            }
        }
    else if (!geometry.surfaces.empty())
        shim::logf("ERROR: effects: lamps: a ship's geometry came without texture coordinates: its nets and gratings cast "
                   "whole shadows");
    std::vector<uint32_t> solid;
    for (size_t i = 0; i < geometry.indices.size(); ++i)
        if (!cut_index[i])
            solid.push_back(geometry.indices[i]);
    const auto buffer = [](const std::vector<uint32_t> &indices) {
        return bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t))),
                                       BGFX_BUFFER_INDEX32);
    };
    if (!solid.empty())
        kept->solid = buffer(solid);
    for (const auto &c : cut)
        kept->cut.emplace_back(c.first, buffer(c.second));
    shim::logf("effects: lamps: a ship's still geometry for its lanterns' shadows: %zu triangles, %zu textures cut holes",
               kept->triangles, kept->cut.size());
}

void LampShadows::upload_grid()
{
    grid_dirty_ = false;
    std::vector<fx::Lamp> lit(slot_.size());
    // The grid covers the place where a lamp with drawn faces reaches it.
    fx::Vec3 lo = geometry_hi_, hi = geometry_lo_;
    for (size_t s = 0; s < slot_.size(); ++s)
    {
        lit[s] = slot_[s].at;
        if (slot_[s].lamp < 0 || !slot_[s].ready) // free, a moving light (the receivers take those without the grid) or not drawn yet
        {
            lit[s].range = 0.0f;
            continue;
        }
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = std::min(lo[k], std::max(geometry_lo_[k], lit[s].position[k] - lit[s].range));
            hi[k] = std::max(hi[k], std::min(geometry_hi_[k], lit[s].position[k] + lit[s].range));
        }
    }
    for (int k = 0; k < 3; ++k)
        if (hi[k] <= lo[k])
            hi[k] = lo[k] + 1.0f;
    grid_ = fx::BuildLampGrid(lit, lo, hi, 1.0f, 64, 16);
    if (bgfx::isValid(grid_map_))
        bgfx::destroy(grid_map_);
    grid_map_ = bgfx::createTexture3D(static_cast<uint16_t>(2 * grid_.nx), static_cast<uint16_t>(grid_.ny),
                                      static_cast<uint16_t>(grid_.nz), false, bgfx::TextureFormat::RGBA8,
                                      BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                      bgfx::copy(grid_.rgba.data(), static_cast<uint32_t>(grid_.rgba.size())));
    grid_uniforms_[0] = {grid_.lo[0], grid_.lo[1], grid_.lo[2], 0.0f};
    grid_uniforms_[1] = {1.0f / (grid_.cell[0] * static_cast<float>(grid_.nx)), 1.0f / (grid_.cell[1] * static_cast<float>(grid_.ny)),
                         1.0f / (grid_.cell[2] * static_cast<float>(grid_.nz)), static_cast<float>(grid_.nx)};
}

void LampShadows::upload_room()
{
    room_dirty_ = false;
    if (bgfx::isValid(room_map_))
        bgfx::destroy(room_map_);
    room_map_ = BGFX_INVALID_HANDLE;
    room_ready_ = !room_.rgba.empty();
    if (!room_ready_)
        return;
    room_map_ = bgfx::createTexture3D(static_cast<uint16_t>(room_.nx), static_cast<uint16_t>(room_.ny),
                                      static_cast<uint16_t>(room_.nz), false, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                      bgfx::copy(room_.rgba.data(), static_cast<uint32_t>(room_.rgba.size())));
    grid_uniforms_[2] = {room_.lo[0], room_.lo[1], room_.lo[2], 0.0f};
    grid_uniforms_[3] = {1.0f / (room_.cell[0] * static_cast<float>(room_.nx)), 1.0f / (room_.cell[1] * static_cast<float>(room_.ny)),
                         1.0f / (room_.cell[2] * static_cast<float>(room_.nz)), 0.0f};
}

bool LampShadows::prepare(shim::Device &device, const std::array<float, 3> &eye, Casters &casters)
{
    if (prepared_)
        return active_;
    prepared_ = true;
    active_ = false;
    ensure_targets();
    if (!enabled())
        return false;
    if (room_dirty_)
        upload_room();
    // Nothing to light with: no lamps of the place, no light that moves, no room light. (At sea there is no still
    // geometry of a location: the ships' lanterns have their ships'.)
    if ((lamps_.empty() || positions_.empty()) && moving_.empty() && ship_lights_.empty() && !room_ready_)
        return false;
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::clamp(std::chrono::duration<float>(now - last_prepare_).count(), 0.0f, 0.1f);
    last_prepare_ = now;

    // A new location (or new lamps): every lamp around the camera is drawn in this frame and shines at once.
    const bool all = layout_dirty_;
    choose_slots(eye);
    choose_moving(eye);
    layout_dirty_ = false;
    draw_static(device, all, casters);
    int used = 0;
    for (Slot &slot : slot_)
    {
        if (slot.lamp == -1)
            continue;
        if (slot.leaving)
        {
            slot.weight -= seconds / kFadeSeconds;
            if (slot.weight <= 0.0f || !slot.ready)
                free_slot(slot);
        }
        else if (slot.ready)
            slot.weight = std::min(1.0f, slot.weight + seconds / kFadeSeconds);
        used += slot.lamp != -1 ? 1 : 0;
    }
    last_slots_ = used;
    if (grid_dirty_)
        upload_grid();

    bgfx::setViewName(kLampDynamicView, "fx lamp shadows (characters)");
    bgfx::setViewMode(kLampDynamicView, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(kLampDynamicView, dynamic_fb_);
    bgfx::setViewRect(kLampDynamicView, 0, 0, static_cast<uint16_t>(columns_ * dynamic_face_size_),
                      static_cast<uint16_t>(rows_ * dynamic_face_size_));
    bgfx::setViewClear(kLampDynamicView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(kLampDynamicView, nullptr, nullptr);
    bgfx::touch(kLampDynamicView);

    // Receiver uniforms of the frame (u_fxLamps, u_fxLampGrid; ff_fragment.sh).
    uniforms_ = {};
    uniforms_[0] = {static_cast<float>(slots_), bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f, gain_ * kBakedGain, baked_};
    uniforms_[1] = {1.0f / static_cast<float>(columns_), 1.0f / static_cast<float>(rows_), static_cast<float>(face_size_),
                    static_cast<float>(dynamic_face_size_)};
    for (int s = 0; s < slots_; ++s)
    {
        const Slot &slot = slot_[s];
        if (slot.lamp == -1 || !slot.ready)
            continue; // radius 0: lights nothing
        // The colour of this frame (flicker) with the slot's fade; position and radius of the slot's faces. w: 1 =
        // characters stand in its light, + 2 = a moving light (it only adds light).
        const fx::Vec3 &color = slot.lamp >= 0 ? lamps_[slot.lamp].color : slot.at.color;
        uniforms_[2 + 3 * s] = {slot.at.position[0], slot.at.position[1], slot.at.position[2], slot.at.range};
        uniforms_[3 + 3 * s] = {color[0] * slot.weight, color[1] * slot.weight, color[2] * slot.weight,
                                (slot.last_casters > 0 ? 1.0f : 0.0f) + (slot.moving ? 2.0f : 0.0f)};
        uniforms_[4 + 3 * s] = {slot.at.att0, slot.at.att1, slot.at.att2, slot.holder};
    }
    grid_uniforms_[4] = {static_cast<float>(static_slots()), static_cast<float>(moving_slots_), tint_, 0.0f};
    // Per moving slot: the turn from the world into the space of its faces (a ship's; else none).
    for (int i = 0; i < 2; ++i)
    {
        const Slot *slot = i < moving_slots_ ? &slot_[static_slots() + i] : nullptr;
        for (int axis = 0; axis < 3; ++axis)
            grid_uniforms_[5 + 3 * i + axis] =
                slot && slot->ship ? std::array<float, 4>{slot->to_local[axis], slot->to_local[4 + axis], slot->to_local[8 + axis], 0.0f}
                                   : std::array<float, 4>{axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f, 0.0f};
    }
    grid_uniforms_[0][3] = static_cast<float>(debug_);
    grid_uniforms_[3][3] = soft_ ? 1.0f : 0.0f;
    active_ = true;
    return true;
}

bool LampShadows::relit(const fx::Vec3 &position) const
{
    if (!active_)
        return false;
    for (const Slot &s : slot_)
        // (A ship's lantern is reported where its ship is drawn: the same place but for the camera-relative rounding.)
        if (s.lamp == kMovingLamp && s.ready && !s.leaving && distance(s.at.position, position) < 0.05f)
            return true;
    for (size_t j = 0; j < raw_.size(); ++j)
        if (raw_lamp_[j] >= 0 && distance(raw_[j].position, position) < 0.02f)
            return std::any_of(slot_.begin(), slot_.end(),
                               [&](const Slot &s) { return s.lamp == raw_lamp_[j] && s.ready && !s.leaving; });
    return false;
}

void LampShadows::draw_faces(shim::Device &device, Casters &casters, int s)
{
    Slot &slot = slot_[s];
    const bgfx::ProgramHandle program = casters.program(device, Casters::Pass::Lamp, false, {}); // logs a failure
    if (!bgfx::isValid(program))
        return;
    // What casts, each part with its texture where that cuts holes: a lamp's triangles were chosen when it took the
    // slot; a carried light's are gathered now, from around where it is; a ship's lantern has the whole ship, in the
    // ship's space.
    struct Part
    {
        shim::BaseTexture *texture = nullptr; // null: solid
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        bgfx::TransientIndexBuffer gathered{}; // when `indices` is not valid
    };
    std::vector<Part> parts;
    bgfx::VertexBufferHandle vertices = static_vb_;
    fx::Lamp lamp = slot.at;
    if (slot.ship)
    {
        const auto ship = std::find_if(ship_geometry_.begin(), ship_geometry_.end(),
                                       [&](const ShipGeometry &g) { return g.ship == slot.ship; });
        vertices = ship != ship_geometry_.end() ? ship->vertices : bgfx::VertexBufferHandle{bgfx::kInvalidHandle};
        lamp.position = slot.local;
        if (bgfx::isValid(vertices))
        {
            if (bgfx::isValid(ship->solid))
                parts.push_back({nullptr, ship->solid});
            for (const auto &cut : ship->cut)
                parts.push_back({cut.first, cut.second});
        }
    }
    else if (slot.moving && bgfx::isValid(static_vb_))
    {
        std::vector<uint32_t> near;
        const auto gather = [&](const fx::TriangleGrid &grid, const std::vector<uint32_t> &triangles, shim::BaseTexture *texture) {
            near.clear();
            fx::TrianglesInSphere(grid, triangles, slot.at.position, slot.at.range, near, seen_);
            const auto count = static_cast<uint32_t>(near.size());
            if (count == 0)
                return;
            if (bgfx::getAvailTransientIndexBuffer(count, true) < count)
            {
                if (!transient_reported_)
                    shim::logf("ERROR: effects: lamps: no room for %u indices of a moving light's still faces in this frame's "
                               "transient buffer: that geometry casts no shadow of it (logged once)",
                               count);
                transient_reported_ = true;
                return;
            }
            Part part{texture};
            bgfx::allocTransientIndexBuffer(&part.gathered, count, true);
            memcpy(part.gathered.data, near.data(), near.size() * sizeof(uint32_t));
            parts.push_back(part);
        };
        gather(solid_grid_, solid_, nullptr);
        for (const CutOut &c : cut_)
            gather(c.grid, c.indices, c.texture);
    }
    else if (bgfx::isValid(static_vb_))
    {
        if (bgfx::isValid(slot.triangles))
            parts.push_back({nullptr, slot.triangles});
        for (const auto &cut : slot.cut)
            parts.push_back({cut_[cut.first].texture, cut.second});
    }
    // The slot's row back to "nothing as far as the lamp reaches": the caster shader writes the distance over the
    // radius, here a distance far beyond any radius.
    const std::array<float, 16> row =
        fx::TileClip({0.0f, static_cast<float>(s) / static_cast<float>(rows_), 1.0f, 1.0f / static_cast<float>(rows_)});
    const float far_away[4] = {static_cast<float>(face_size_), 1e9f, 0.0f, fx::kLampFaceGuard};
    bgfx::setScissor(0, static_cast<uint16_t>(s * face_size_), static_cast<uint16_t>(columns_ * face_size_),
                     static_cast<uint16_t>(face_size_));
    bgfx::setUniform(u_caster_, far_away);
    bgfx::setTransform(row.data());
    bgfx::setVertexBuffer(0, clear_vb_);
    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(kLampStaticView, program);
    const float caster[4] = {static_cast<float>(face_size_), 1.0f / slot.at.range, slot.holder, fx::kLampFaceGuard};
    for (int face = 0; face < 6; ++face)
    {
        const std::array<float, 16> m = face_matrix(lamp, s, face, columns_, rows_);
        for (const Part &part : parts) // none: the cleared row is all there is
        {
            // Solid, or with its texture, so the shadow has its holes (foliage, nets, gratings): the same rule as for
            // a draw, the still geometry's material with this texture (sets the cut's uniforms).
            shim::TexStore *store = part.texture ? shim::store_of(part.texture) : nullptr;
            if (part.texture && !store)
                continue;
            const bgfx::ProgramHandle program_part =
                store ? casters.program(device, Casters::Pass::Lamp, false, location_caster_material(store)) : program;
            if (!bgfx::isValid(program_part))
                continue;
            if (store)
            {
                store->sync();
                bgfx::setTexture(0, casters.sampler(), store->handle);
            }
            scissor_tile(s, face, columns_, face_size_);
            bgfx::setUniform(u_caster_, caster);
            bgfx::setTransform(m.data());
            bgfx::setVertexBuffer(0, vertices);
            if (bgfx::isValid(part.indices))
                bgfx::setIndexBuffer(part.indices);
            else
                bgfx::setIndexBuffer(&part.gathered);
            bgfx::setState(kCasterState);
            bgfx::submit(kLampStaticView, program_part);
        }
    }
    static_faces_ += 6;
}

void LampShadows::draw_static(shim::Device &device, bool all, Casters &casters)
{
    if (std::none_of(slot_.begin(), slot_.end(),
                     [](const Slot &s) { return (s.lamp >= 0 && !s.ready) || (s.lamp == kMovingLamp && s.redraw); }))
        return;
    if (geometry_dirty_ && bgfx::isValid(static_vb_))
    {
        bgfx::destroy(static_vb_);
        static_vb_ = BGFX_INVALID_HANDLE;
    }
    if (!bgfx::isValid(static_vb_) && !positions_.empty())
    {
        // Position and the texture coordinates of the cut-out casters (zero when the location gave none).
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
        const bgfx::Memory *vertices = bgfx::alloc(static_cast<uint32_t>(positions_.size() * 5 * sizeof(float)));
        auto *out = reinterpret_cast<float *>(vertices->data);
        for (size_t i = 0; i < positions_.size(); ++i, out += 5)
        {
            memcpy(out, positions_[i].data(), 3 * sizeof(float));
            out[3] = uvs_.size() == positions_.size() ? uvs_[i][0] : 0.0f;
            out[4] = uvs_.size() == positions_.size() ? uvs_[i][1] : 0.0f;
        }
        static_vb_ = bgfx::createVertexBuffer(vertices, layout);
    }
    geometry_dirty_ = false;
    bgfx::setViewName(kLampStaticView, "fx lamp shadows (location)");
    bgfx::setViewMode(kLampStaticView, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(kLampStaticView, static_fb_);
    bgfx::setViewRect(kLampStaticView, 0, 0, static_cast<uint16_t>(columns_ * face_size_), static_cast<uint16_t>(rows_ * face_size_));
    bgfx::setViewClear(kLampStaticView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(kLampStaticView, nullptr, nullptr);
    int drawn = 0;
    for (int s = 0; s < slots_; ++s)
    {
        Slot &slot = slot_[s];
        if (slot.lamp == kMovingLamp)
        {
            // A light that moves: every time it has moved (and at once when it appears). A ship's lantern: once its
            // ship's geometry is here (the ship is asked for it: ship_geometry_wanted), and when that changes.
            if (!slot.redraw || (slot.ship && std::none_of(ship_geometry_.begin(), ship_geometry_.end(), [&](const ShipGeometry &g) {
                                     return g.ship == slot.ship;
                                 })))
                continue;
            draw_faces(device, casters, s);
            slot.redraw = false;
            slot.ready = true;
            continue;
        }
        if (slot.lamp < 0 || slot.ready || (!all && drawn >= kSlotsPerFrame))
            continue;
        draw_faces(device, casters, s);
        slot.ready = true;
        if (all)
            slot.weight = 1.0f;
        grid_dirty_ = true;
        ++drawn;
    }
    if (all && !reported_)
    {
        reported_ = true;
        upload_grid();
        size_t cut_triangles = 0;
        for (const CutOut &c : cut_)
            cut_triangles += c.indices.size() / 3;
        shim::logf("effects: lamps: %s, %d of %zu lamps (%zu lights before merging fixtures) have a slot, %d slots kept for "
                   "moving lights, faces %d px, characters %d px; grid %dx%dx%d cells, at most %d lamps over a cell, %d "
                   "cells over the %d held; room light %dx%dx%d; %zu textures cut holes into %zu triangles",
                   interior_ ? "interior" : "outdoors", drawn, lamps_.size(), raw_.size(), moving_slots_, face_size_,
                   dynamic_face_size_, grid_.nx, grid_.ny, grid_.nz, grid_.max_lamps, grid_.overflow_cells, fx::kLampGridCapacity,
                   room_.nx, room_.ny, room_.nz, cut_.size(), cut_triangles);
    }
}

void LampShadows::submit_caster(shim::Device &device, const float *world, bool skinned, const std::array<float, 3> &center,
                                float radius, Casters &casters, const CasterMaterial &material)
{
    if (!active_)
        return;
    const bgfx::ProgramHandle program = casters.program(device, Casters::Pass::Lamp, skinned, material);
    if (!bgfx::isValid(program))
        return;
    for (int s = 0; s < slots_; ++s)
    {
        Slot &slot = slot_[s];
        const fx::Lamp &l = slot.at;
        if (slot.lamp == -1 || !slot.ready || distance(l.position, {center[0], center[1], center[2]}) > l.range + radius)
            continue;
        // A ship's own still geometry is in the still faces of its lanterns.
        if (slot.ship && slot.ship == drawn_ship_)
            continue;
        // The faces of a ship's lantern are in the ship's space.
        fx::Lamp in_faces = l;
        fx::Vec3 at{center[0], center[1], center[2]};
        std::array<float, 16> to_faces{};
        memcpy(to_faces.data(), world, sizeof(to_faces));
        if (slot.ship)
        {
            in_faces.position = slot.local;
            bx::mtxMul(to_faces.data(), world, slot.to_local.data());
            const float *t = slot.to_local.data();
            at = {center[0] * t[0] + center[1] * t[4] + center[2] * t[8] + t[12],
                  center[0] * t[1] + center[1] * t[5] + center[2] * t[9] + t[13],
                  center[0] * t[2] + center[1] * t[6] + center[2] * t[10] + t[14]};
        }
        ++slot.casters;
        for (int face = 0; face < 6; ++face)
        {
            if (!fx::SphereInLampFace(in_faces.position, face, at, radius))
                continue;
            const std::array<float, 16> m = face_matrix(in_faces, s, face, columns_, rows_);
            std::array<float, 16> wm{};
            bx::mtxMul(wm.data(), to_faces.data(), m.data());
            // A lamp's own model (a street lantern's, a ship lantern's) and the hand that carries a lantern cast no
            // shadow of it.
            const float caster[4] = {static_cast<float>(dynamic_face_size_), 1.0f / l.range, std::max(slot.holder, kLanternHolder),
                                     fx::kLampFaceGuard};
            scissor_tile(s, face, columns_, dynamic_face_size_);
            bgfx::setUniform(u_caster_, caster);
            bgfx::setStencil(BGFX_STENCIL_NONE);
            bgfx::setTransform(wm.data());
            bgfx::setState(kCasterState);
            // The draw's buffers and bone palette stay for the next copy; Lighting::submit_caster discards at the end.
            bgfx::submit(kLampDynamicView, program, 0, BGFX_DISCARD_NONE);
        }
    }
}

bool LampShadows::reaches(const std::array<float, 3> &center, float radius) const
{
    if (!active_)
        return false;
    return std::any_of(slot_.begin(), slot_.end(), [&](const Slot &s) {
        return s.lamp != -1 && s.ready && distance(s.at.position, {center[0], center[1], center[2]}) <= s.at.range + radius;
    });
}

void LampShadows::bind(bool active, const std::array<float, 16> &view, bool room)
{
    ensure_targets();
    const bool on = active && active_;
    if (view != bound_view_)
    {
        bx::mtxInverse(view_to_world_.data(), view.data());
        bound_view_ = view;
    }
    static const std::array<std::array<float, 4>, 2 + 3 * fx::kMaxShadowLamps> none{};
    bgfx::setUniform(u_view_, view_to_world_.data());
    bgfx::setUniform(u_lamps_, on ? uniforms_.data() : none.data(), static_cast<uint16_t>(uniforms_.size()));
    auto grid = grid_uniforms_;
    const bool room_on = on && room && room_ready_;
    grid[2][3] = room_on ? 1.0f + model_lift_ : 0.0f;
    bgfx::setUniform(u_grid_, grid.data(), static_cast<uint16_t>(grid.size()));
    const uint32_t sampler = BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::setTexture(fx::kLampDynamicStage, s_dynamic_, on ? dynamic_map_ : open_map_, sampler);
    bgfx::setTexture(fx::kLampStaticStage, s_static_, on ? static_map_ : open_map_, sampler);
    bgfx::setTexture(fx::kLampGridStage, s_grid_, on && bgfx::isValid(grid_map_) ? grid_map_ : open_grid_,
                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setTexture(fx::kRoomLightStage, s_room_, room_on ? room_map_ : open_grid_, BGFX_SAMPLER_UVW_CLAMP);
}

void LampShadows::end_frame()
{
    if (active_)
        for (Slot &slot : slot_)
        {
            slot.last_casters = slot.casters;
            slot.casters = 0;
        }
    last_static_faces_ = static_faces_;
    static_faces_ = 0;
    // Lights that move are reported every frame they exist.
    if (!moving_this_frame_)
        moving_.clear();
    if (!ship_lights_this_frame_)
    {
        ship_lights_.clear();
        ships_.clear();
    }
    moving_this_frame_ = ship_lights_this_frame_ = false;
    if (!prepared_)
        last_slots_ = 0;
    prepared_ = false;
    active_ = false;
}

void LampShadows::shutdown()
{
    for (bgfx::FrameBufferHandle *fb : {&static_fb_, &dynamic_fb_})
        if (bgfx::isValid(*fb))
            bgfx::destroy(*fb);
    for (bgfx::TextureHandle *t : {&open_map_, &grid_map_, &room_map_, &open_grid_})
        if (bgfx::isValid(*t))
            bgfx::destroy(*t);
    for (bgfx::VertexBufferHandle *b : {&static_vb_, &clear_vb_})
        if (bgfx::isValid(*b))
            bgfx::destroy(*b);
    for (Slot &slot : slot_)
        free_slot(slot);
    for (ShipGeometry &geometry : ship_geometry_)
        free_ship_geometry(geometry);
    for (bgfx::UniformHandle *u : {&s_static_, &s_dynamic_, &s_grid_, &s_room_, &u_view_, &u_lamps_, &u_grid_, &u_caster_})
        if (bgfx::isValid(*u))
            bgfx::destroy(*u);
    on_device_lost();
}

void LampShadows::on_device_lost()
{
    static_fb_ = dynamic_fb_ = BGFX_INVALID_HANDLE;
    static_map_ = dynamic_map_ = open_map_ = grid_map_ = room_map_ = open_grid_ = BGFX_INVALID_HANDLE;
    static_vb_ = clear_vb_ = BGFX_INVALID_HANDLE;
    s_static_ = s_dynamic_ = s_grid_ = s_room_ = u_view_ = u_lamps_ = u_grid_ = u_caster_ = BGFX_INVALID_HANDLE;
    slot_.assign(slot_.size(), Slot{});
    for (int s = static_slots(); s < slots_; ++s)
        slot_[s].moving = true;
    ship_geometry_.clear();
    layout_dirty_ = grid_dirty_ = geometry_dirty_ = true;
    room_dirty_ = true;
    room_ready_ = false;
    bound_view_ = {};
}

} // namespace storm::effects
