// The lamps of a location relight it per pixel, each with its own shadow maps (fx/lamp_shadows.hpp,
// Settings::lamp_shadows, option fx_lamp_shadows).
//
// The location reports its lamps every frame (render::SetEffectsLamps: the static lights of lights.ini placed at
// its locators, with their flicker) and once its still geometry with its baked vertex colours
// (render::SetEffectsLampGeometry). Candles of one fixture are merged; every lamp gets the radius within which it
// lights (fx::LampRadius). The lamps nearest to the camera, up to Settings::lamp_shadows, take a slot:
//   * a row of six cube faces in two depth atlases holding the distance to the lamp / its radius (linear, written
//     by fs_ff_lamp_caster): `static` with the still geometry around the lamp, drawn once when the lamp takes the
//     slot (a few slots per frame; all of them in the first frame of a location); `dynamic` with the characters (the
//     skinned lit draws, submitted like the sun casters), redrawn every frame into the faces their sphere reaches;
//   * its number in the lamp grid (a small 3D texture: which slots reach which cell of the place).
// A lamp fades in once its faces are drawn and fades out before its slot goes to another lamp.
// Receivers (the lit and baked variants into the game picture, ff_fragment.sh ffLampLight) look the lamps of their
// cell up, evaluate each with the game's own attenuation and compare against both atlases. A lamp without a slot
// lights nothing per pixel: no light without its shadow.
// Interiors also get the room light (fx::RoomLight, a 3D texture of the baked colours around every point): the
// ambient of the lit models that stand in the room.
// All positions here are absolute world. The textures are always bound (1x1 open ones when there is nothing), since
// GL refuses a draw that leaves a shadow or 3D sampler on a unit of another type.
#pragma once

#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/settings.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <chrono>
#include <vector>

namespace storm::shim
{
struct Device;
}

namespace storm::effects
{

class LampShadows
{
  public:
    void configure(const fx::Settings &settings);
    bool enabled() const { return slots_ > 0; }
    // An interior is loaded (its still geometry was reported): the weather's sun and ambient are not its light.
    bool interior() const { return interior_ && !positions_.empty(); }
    // Main thread, every frame the location draws: its lamps (empty when it has none).
    void set_lamps(const std::vector<fx::Lamp> &lamps);
    // The still geometry of the location with its baked vertex colours (D3DCOLOR per position); empty: none (the sea).
    // An interior's room light covers room_lo..room_hi.
    void set_geometry(std::vector<fx::Vec3> positions, std::vector<uint32_t> colors, std::vector<uint32_t> indices, bool interior,
                      const fx::Vec3 &room_lo, const fx::Vec3 &room_hi);
    // The first receiver of a frame: gives the lamps around the camera at `eye` their slots, renders static faces
    // that are due and opens the dynamic atlas. True when this frame's receivers take lamps or the room light.
    bool prepare(shim::Device &device, const std::array<float, 3> &eye);
    // A D3D point light at `position` is one of the place's lamps that has a slot this frame: the receivers light it
    // per pixel with its shadow, so it leaves the draw's own light sums.
    bool relit(const fx::Vec3 &position) const;
    // This frame's lit models take their ambient from the room light.
    bool room_light() const { return active_ && room_ready_; }
    // A character draw (buffers and bone palette still bound): into the dynamic faces its sphere reaches.
    void submit_caster(shim::Device &device, const float *world, bool skinned, const std::array<float, 3> &center,
                       float radius);
    // Receiver uniforms and textures for a draw with the camera `view` (row-vector absolute world -> view);
    // `active` false: the open textures and no lamps. `room`: the draw's ambient is the room light.
    void bind(bool active, const std::array<float, 16> &view, bool room);
    void end_frame();
    bool prepare_programs(shim::Device &device, int &budget);
    // Lamps with a slot and static faces drawn in the last frame (effects profile).
    int last_slots() const { return last_slots_; }
    int last_static_faces() const { return last_static_faces_; }

    void shutdown();
    void on_device_lost();

  private:
    struct Slot
    {
        int lamp = -1;          // index into lamps_, -1: free
        fx::Lamp at{};          // the lamp its faces were drawn for (range = its radius)
        float holder = 0.0f;    // metres around it in which the still geometry casts nothing
        bool ready = false;     // its static faces are drawn
        bool leaving = false;   // fading out: the slot goes to another lamp
        float weight = 0.0f;    // 0..1, the fade
        bgfx::IndexBufferHandle triangles = BGFX_INVALID_HANDLE; // the still geometry within its radius
        uint32_t casters = 0, last_casters = 0;                  // character draws this / the last frame
    };

    void ensure_targets();
    void choose_slots(const std::array<float, 3> &eye);
    void free_slot(Slot &slot);
    void draw_static(shim::Device &device, bool all);
    void upload_grid();
    void upload_room();
    void find_shaders();

    int slots_ = 0;
    int face_size_ = 0, dynamic_face_size_ = 0;
    int columns_ = 0, rows_ = 0;
    float gain_ = 1.0f, baked_ = 0.5f;
    bool soft_ = false;
    int debug_ = 0;
    std::vector<fx::Lamp> raw_;   // as reported this frame
    std::vector<int> raw_lamp_;   // per raw lamp: its merged lamp (index into lamps_), -1: none (no light)
    std::vector<fx::Lamp> lamps_; // merged, this frame's colours; range = the radius it lights
    std::vector<fx::Vec3> placed_; // per merged lamp: where it stood when the slots were laid out
    std::vector<float> radius_;   // per merged lamp, fixed while the lamps stand where they were
    std::vector<float> holder_;   // per merged lamp: the size of its fixture
    std::vector<Slot> slot_;
    bool layout_dirty_ = true; // the lamps or the geometry changed: every slot is chosen and drawn again at once
    bool grid_dirty_ = true;
    bool prepared_ = false, active_ = false;
    bool geometry_dirty_ = false, room_dirty_ = false, room_ready_ = false;
    bool interior_ = false;
    std::vector<fx::Vec3> positions_;
    std::vector<uint32_t> colors_;
    std::vector<uint32_t> indices_;
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
    std::array<std::array<float, 4>, 4> grid_uniforms_{};
    std::array<float, 16> bound_view_{}, view_to_world_{};
    uint32_t vs_caster_[2] = {};
    uint32_t fs_caster_ = 0;
    bool reported_ = false;
};

} // namespace storm::effects
