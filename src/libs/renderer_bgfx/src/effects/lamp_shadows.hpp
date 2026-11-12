// Lamp shadows of interiors (fx/lamp_shadows.hpp, Settings::lamp_shadows, option fx_lamp_shadows).
//
// The location reports its lamps every frame (render::SetEffectsLamps: the static lights of lights.ini placed at
// its locators, with their flicker) and, through the AO volume, its still collidable geometry. The strongest
// lamps around the camera (Средняя 2, Высокая 4; candles of one candlestick merged) get the six faces of a depth cube
// in two atlases:
//   static  - the location geometry, drawn when a lamp takes its slot (pre-game view kLampStaticView);
//   dynamic - the characters (the skinned lit draws, submitted like the sun casters), redrawn every frame
//             (pre-game view kLampDynamicView), only into the faces their sphere reaches.
// Receivers (the lit variant into the game picture: characters and props; baked surfaces, whose bakes carry little lamp
// light, receive none) remove the lamp's light where either atlas hides it (ff_fragment.sh ffLampVisibility). Both atlases are always bound at stages 10 / 11 (a 1x1 open depth
// texture when there are none), since GL refuses a draw that leaves a shadow sampler on a unit of another type.
#pragma once

#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/settings.hpp"

#include <bgfx/bgfx.h>

#include <array>
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
    // An interior is loaded (its still geometry was reported).
    bool has_geometry() const { return !positions_.empty(); }
    // Main thread, every frame the location draws: its lamps (empty outdoors).
    void set_lamps(std::vector<fx::Lamp> lamps);
    // The still geometry of the interior (the AO volume's); empty: outdoors.
    void set_geometry(const std::vector<fx::Vec3> &positions, const std::vector<uint32_t> &indices);
    // The first lit draw into the game picture of a frame: chooses the lamps for the camera at `eye`, renders the
    // static faces of new slots and opens the dynamic atlas. True when this frame has lamp shadows.
    bool prepare(shim::Device &device, const std::array<float, 3> &eye);
    // A character draw (buffers and bone palette still bound): into the dynamic faces its sphere reaches.
    void submit_caster(shim::Device &device, const float *world, bool skinned, const std::array<float, 3> &center,
                       float radius);
    // Receiver uniforms and textures for a draw with the camera `view` (row-vector world -> view); `active` false:
    // the open atlases and no lamps.
    void bind(bool active, const std::array<float, 16> &view);
    void end_frame();
    bool prepare_programs(shim::Device &device, int &budget);

    void shutdown();
    void on_device_lost();

  private:
    void ensure_targets();
    void draw_static(shim::Device &device);
    void find_shaders();

    int slots_ = 0;
    int face_size_ = 0, dynamic_face_size_ = 0;
    int columns_ = 0, rows_ = 0;
    std::vector<fx::Lamp> lamps_;      // clustered, this frame
    std::vector<int> chosen_;          // indices into lamps_ of the active slots
    std::vector<fx::Lamp> slot_lamps_; // the lamps whose static faces the atlas holds
    bool static_dirty_ = true;
    bool prepared_ = false, active_ = false;
    bool geometry_dirty_ = false;
    std::vector<fx::Vec3> positions_;
    std::vector<uint32_t> indices_;
    bgfx::VertexBufferHandle static_vb_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle static_ib_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle static_map_ = BGFX_INVALID_HANDLE, dynamic_map_ = BGFX_INVALID_HANDLE, open_map_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle static_fb_ = BGFX_INVALID_HANDLE, dynamic_fb_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_static_ = BGFX_INVALID_HANDLE, s_dynamic_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_view_ = BGFX_INVALID_HANDLE, u_lamps_ = BGFX_INVALID_HANDLE;
    std::array<std::array<float, 4>, 1 + 4 * fx::kMaxShadowLamps> uniforms_{};
    std::array<float, 16> bound_view_{}, view_to_world_{};
    uint32_t vs_caster_[2] = {};
    uint32_t fs_caster_ = 0;
    bool reported_ = false;
};

} // namespace storm::effects
