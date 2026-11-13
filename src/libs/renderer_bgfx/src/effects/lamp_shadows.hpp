// The lamps of a location relight it per pixel, each with its own shadow maps (fx/lamp_shadows.hpp,
// Settings::lamp_shadows, option fx_lamp_shadows).
//
// The location reports its lamps every frame (render::SetEffectsLamps: the static lights of lights.ini placed at
// its locators, with their flicker) and once its still geometry with its baked vertex colours
// (render::SetEffectsLocationGeometry). Candles of one fixture are merged; every lamp gets the radius within which it
// lights (fx::LampRadius). The lamps nearest to the camera, up to Settings::lamp_shadows, take a slot:
//   * a row of six cube faces in two depth atlases holding the distance to the lamp / its radius (linear, written
//     by fs_ff_lamp_caster): `static` with the still geometry around the lamp, drawn once when the lamp takes the
//     slot (a few slots per frame; all of them in the first frame of a location); `dynamic` with the characters (the
//     skinned lit draws, submitted like the sun casters), redrawn every frame into the faces their sphere reaches;
//   * its number in the lamp grid (a small 3D texture: which slots reach which cell of the place).
// A lamp fades in once its faces are drawn and fades out before its slot goes to another lamp.
// The last Settings::lamp_moving slots are for the lights that move, the nearest to the camera; the receivers take
// them without the grid, and they only add light (no bake holds theirs), as far and as strong as the game's point
// light they stand for (its D3D range and attenuation, the fade of fxPointLight):
//   * carried lanterns and torches (render::SetEffectsDynamicLights): their still faces are drawn again from the
//     location's triangles around them whenever they move;
//   * the ships' lanterns (render::SetEffectsShipLights): a lantern stands still on its ship, so its faces are in
//     the ship's space (the receivers and the casters turn into it): the still faces hold the ship itself
//     (render::SetEffectsShipGeometry, drawn once and when the ship changes), the dynamic ones the crew, the sails
//     and other ships.
// Receivers (the lit and baked variants into the game picture, ff_fragment.sh ffLampLight) look the lamps of their
// cell up, evaluate each with the game's own attenuation and compare against both atlases. A lamp without a slot
// lights nothing per pixel: no light without its shadow.
// Interiors also get the room light (fx::RoomLight, a 3D texture of the baked colours around every point): the
// ambient of the lit models that stand in the room.
// All positions here are absolute world. The textures are always bound (1x1 open ones when there is nothing), since
// GL refuses a draw that leaves a shadow or 3D sampler on a unit of another type.
#pragma once

#include "effects/casters.hpp"
#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/settings.hpp"
#include "storm/render/effects.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <chrono>
#include <utility>
#include <vector>

namespace storm::shim
{
struct Device;
struct BaseTexture;
} // namespace storm::shim

namespace storm::effects
{

class LampShadows
{
  public:
    void configure(const fx::Settings &settings);
    bool enabled() const { return slots_ > 0; }
    // True when a lit draw whose bounding sphere is this may cast into a slot's dynamic faces (it reaches a lamp
    // that has a slot).
    bool reaches(const std::array<float, 3> &center, float radius) const;
    // An interior is loaded (its still geometry was reported): the weather's sun and ambient are not its light.
    bool interior() const { return interior_ && !positions_.empty(); }
    // Main thread, every frame the location draws: its lamps (empty when it has none).
    void set_lamps(const std::vector<fx::Lamp> &lamps);
    // The place's lamps as the relight lights them (merged fixtures, range = the radius it windows them to), their
    // fixtures' sizes, and a count that changes whenever they are laid out anew (they came, went, moved, their radii
    // changed; not their flicker): the bounce of their light (effects/bounce.hpp) follows it.
    const std::vector<fx::Lamp> &lamps() const { return lamps_; }
    const std::vector<float> &holders() const { return holder_; }
    uint32_t layout() const { return layout_; }
    // Main thread, every frame the location draws: the lights that move (carried lanterns and torches).
    void set_moving(std::vector<fx::Lamp> lamps)
    {
        moving_ = std::move(lamps);
        moving_this_frame_ = true;
    }
    // ... and the lanterns of the ships at sea with their ships (render::SetEffectsShipLights).
    struct ShipLight
    {
        fx::Lamp lamp;     // absolute world
        uint64_t ship = 0;
        fx::Vec3 local{};  // in the ship's space
    };
    void set_ship_lights(std::vector<render::EffectsShip> ships, std::vector<ShipLight> lights)
    {
        ships_ = std::move(ships);
        ship_lights_ = std::move(lights);
        ship_lights_this_frame_ = true;
    }
    // render::EffectsShipGeometryWanted / SetEffectsShipGeometry.
    bool ship_geometry_wanted(uint64_t ship, uint64_t signature) const;
    void set_ship_geometry(uint64_t ship, uint64_t signature, const render::EffectsShipGeometry &geometry);
    // The draws that follow are the still geometry of this ship (0: of none).
    void set_drawn_ship(uint64_t ship) { drawn_ship_ = ship; }
    // The still geometry of the location (render::EffectsLocationGeometry); an empty one: none (the sea).
    void set_geometry(const render::EffectsLocationGeometry &geometry);
    // The first receiver of a frame: gives the lamps around the camera at `eye` their slots, renders static faces
    // that are due and opens the dynamic atlas. True when this frame's receivers take lamps or the room light.
    bool prepare(shim::Device &device, const std::array<float, 3> &eye, Casters &casters);
    // A point light at `position` is one of the place's lamps, or a moving light, that has a slot this frame: the
    // receivers light it per pixel with its shadow, so it leaves the draw's own light sums.
    bool relit(const fx::Vec3 &position) const;
    // This frame's lit models take their ambient from the room light.
    bool room_light() const { return active_ && room_ready_; }
    // A lit draw (a character, a prop, a ship's hull, mast or sail: whatever is not part of the still geometry; its
    // buffers, textures and bone palette still bound): into the dynamic faces its sphere reaches; `material`: how it
    // casts (effects/casters.hpp).
    void submit_caster(shim::Device &device, const float *world, bool skinned, const std::array<float, 3> &center,
                       float radius, Casters &casters, const CasterMaterial &material);
    // Receiver uniforms and textures for a draw with the camera `view` (row-vector absolute world -> view);
    // `active` false: the open textures and no lamps. `room`: the draw's ambient is the room light.
    void bind(bool active, const std::array<float, 16> &view, bool room);
    void end_frame();
    // Lamps with a slot and static faces drawn in the last frame (effects profile).
    int last_slots() const { return last_slots_; }
    int last_static_faces() const { return last_static_faces_; }

    void shutdown();
    void on_device_lost();

  private:
    struct Slot
    {
        int lamp = -1;          // index into lamps_, -1: free (a moving slot: kMovingLamp while it holds a light)
        bool moving = false;    // one of the slots kept for lights that move: its still faces follow the light
        fx::Lamp at{};          // the lamp its faces were drawn for (range = its radius)
        float holder = 0.0f;    // metres around it in which the still geometry casts nothing
        bool ready = false;     // its static faces are drawn
        bool redraw = false;    // a moving light has moved, a ship's geometry came: its still faces are drawn again
        uint64_t ship = 0;      // a ship's lantern: the ship (its faces are in the ship's space)
        fx::Vec3 local{};       // ... the lantern in that space
        std::array<float, 16> to_local{}; // ... absolute world -> that space (row vectors), this frame
        bool leaving = false;   // fading out: the slot goes to another lamp
        float weight = 0.0f;    // 0..1, the fade
        bgfx::IndexBufferHandle triangles = BGFX_INVALID_HANDLE; // the solid still geometry within its radius
        std::vector<std::pair<size_t, bgfx::IndexBufferHandle>> cut; // ... and of each cut-out texture (index into cut_)
        uint32_t casters = 0, last_casters = 0;                  // character draws this / the last frame
    };

    void ensure_targets();
    void choose_slots(const std::array<float, 3> &eye);
    void free_slot(Slot &slot);
    void draw_static(shim::Device &device, bool all, Casters &casters);
    void choose_moving(const std::array<float, 3> &eye);
    // The six still faces of `slot` (its row cleared first) from `triangles` / `cut` index buffers.
    void draw_faces(shim::Device &device, Casters &casters, int slot);
    static constexpr int kMovingLamp = -2;
    int static_slots() const { return slots_ - moving_slots_; }
    void upload_grid();
    void upload_room();

    int slots_ = 0;
    int face_size_ = 0, dynamic_face_size_ = 0;
    int columns_ = 0, rows_ = 0;
    float gain_ = 1.0f, baked_ = 0.5f;
    bool soft_ = false;
    int debug_ = 0;
    int moving_slots_ = 0;         // the last slots of the atlas
    std::vector<fx::Lamp> moving_; // the lights that move, as reported this frame
    std::vector<ShipLight> ship_lights_;
    std::vector<render::EffectsShip> ships_;
    // The still geometry of the ships whose lanterns hold a slot, in each ship's space.
    struct ShipGeometry
    {
        uint64_t ship = 0, signature = 0;
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle solid = BGFX_INVALID_HANDLE;
        std::vector<std::pair<shim::BaseTexture *, bgfx::IndexBufferHandle>> cut; // per texture that cuts holes
        size_t triangles = 0;
    };
    std::vector<ShipGeometry> ship_geometry_;
    void free_ship_geometry(ShipGeometry &geometry);
    uint64_t drawn_ship_ = 0;
    bool moving_this_frame_ = false, ship_lights_this_frame_ = false; // reported since the last Present (else: none)
    std::vector<fx::Lamp> raw_;   // as reported this frame
    std::vector<int> raw_lamp_;   // per raw lamp: its merged lamp (index into lamps_), -1: none (no light)
    std::vector<fx::Lamp> lamps_; // merged, this frame's colours; range = the radius it lights
    std::vector<fx::Vec3> placed_; // per merged lamp: where it stood when the slots were laid out
    std::vector<float> radius_;   // per merged lamp, fixed while the lamps stand where they were
    std::vector<float> holder_;   // per merged lamp: the size of its fixture
    uint32_t layout_ = 0;         // see layout()
    float layout_gain_ = -1.0f;   // the gain of the last layout
    std::vector<Slot> slot_;
    bool layout_dirty_ = true; // the lamps or the geometry changed: every slot is chosen and drawn again at once
    bool grid_dirty_ = true;
    bool prepared_ = false, active_ = false;
    bool geometry_dirty_ = false, room_dirty_ = false, room_ready_ = false;
    bool interior_ = false;
    std::vector<fx::Vec3> positions_;
    std::vector<uint32_t> colors_;
    std::vector<std::array<float, 2>> uvs_;
    std::vector<uint32_t> indices_; // every triangle (the room light)
    std::vector<uint32_t> solid_;   // the triangles that cast whole
    fx::TriangleGrid solid_grid_;   // ... by where they are
    std::vector<uint32_t> seen_;    // scratch of fx::TrianglesInSphere
    struct CutOut                   // the triangles of one texture whose alpha cuts holes
    {
        shim::BaseTexture *texture = nullptr;
        std::vector<uint32_t> indices;
        fx::TriangleGrid grid;
    };
    std::vector<CutOut> cut_;
    float model_lift_ = 0.0f;
    float tint_ = 0.25f; // Settings::lamp_tint
    fx::Vec3 geometry_lo_{}, geometry_hi_{};
    fx::Vec3 room_lo_{}, room_hi_{};
    fx::LampGrid grid_;
    fx::RoomLight room_;
    std::chrono::steady_clock::time_point last_prepare_{};
    int last_slots_ = 0, last_static_faces_ = 0, static_faces_ = 0;
    bgfx::VertexBufferHandle static_vb_ = BGFX_INVALID_HANDLE, clear_vb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle static_map_ = BGFX_INVALID_HANDLE, dynamic_map_ = BGFX_INVALID_HANDLE, open_map_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle grid_map_ = BGFX_INVALID_HANDLE, room_map_ = BGFX_INVALID_HANDLE, open_grid_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle static_fb_ = BGFX_INVALID_HANDLE, dynamic_fb_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_static_ = BGFX_INVALID_HANDLE, s_dynamic_ = BGFX_INVALID_HANDLE, s_grid_ = BGFX_INVALID_HANDLE,
                        s_room_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_view_ = BGFX_INVALID_HANDLE, u_lamps_ = BGFX_INVALID_HANDLE, u_grid_ = BGFX_INVALID_HANDLE,
                        u_caster_ = BGFX_INVALID_HANDLE;
    std::array<std::array<float, 4>, 2 + 3 * fx::kMaxShadowLamps> uniforms_{};
    std::array<std::array<float, 4>, 11> grid_uniforms_{};
    std::array<float, 16> bound_view_{}, view_to_world_{};
    bool reported_ = false;
    bool transient_reported_ = false, ship_reported_ = false;
};

} // namespace storm::effects
