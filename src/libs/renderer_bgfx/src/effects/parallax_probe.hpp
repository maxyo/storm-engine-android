// The parallax probe (fx_pom_probe; fx/parallax_probe.hpp): the relief texture under the screen centre, for the owner's
// tuning of the parallax per texture (fx_pom_toggle, NormalMaps::toggle_parallax). The renderer draws what it finds
// over the picture (render::EffectsParallaxProbe).
//
// Exact, on the GPU: a relief draw into the game picture that writes depth and whose bounding sphere lies on the
// centre ray (fx::OnCentreRay; the sphere is computed only while the probe is on) is submitted a second time
// (shim::Device::draw, its buffers kept) into kView with the same program, uniforms and textures, depth test EQUAL, no
// writes, a one-pixel scissor at the centre and an occlusion query. kView is placed right after the frame's last scene
// view (bgfx::setViewOrder, place()), so it tests against the scene's final depth before the post chain or the
// interface touch it: the draw that is in front at the centre passes, a hidden one fails, an alpha-tested hole (a
// fence) discards as in the scene. A CPU ray against bounds would pick a table seen through a wall's window frame, or
// the wall behind a lamp post, by its sphere; against the triangles it would need the skinned and animated vertices
// and the alpha test the GPU already has.
// The queries of a frame are read kBatches frames later (bgfx keeps their last result); a frame whose queries are not
// all back, or that drew no scene (a held pause picture), decides nothing. Nothing of this runs while the probe is off.
#pragma once

#include "storm/fx/parallax_probe.hpp"
#include "storm/render/effects.hpp"
#include "storm/render/frame_owner.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace storm::effects
{

class NormalMaps;
struct NormalMapEntry;

class ParallaxProbe
{
  public:
    // The last pre-game view (unused by the passes before the scene), moved behind the scene each frame it is used.
    static constexpr bgfx::ViewId kView = render::kPreGameViewBegin + render::kPreGameViewCount - 1;
    static constexpr int kBatches = 6;    // frames between a query's submission and its reading
    static constexpr int kPerFrame = 32;  // probe draws a frame at most (logged once when more are on the ray)

    // fx_pom_probe (Host::apply). Off releases the queries and restores the view order.
    void set_enabled(bool on);
    bool enabled() const { return enabled_; }
    // fx_pom_toggle's edge (Host::apply): switches the parallax of the texture under the centre (logged).
    void toggle(NormalMaps &normal_maps);

    // The view a probe draw comes from: kView gets the same.
    struct Target
    {
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        std::array<uint16_t, 4> rect{}; // x, y, w, h
        float min_z = 0.0f, max_z = 1.0f;
        bool operator==(const Target &o) const
        {
            return framebuffer.idx == o.framebuffer.idx && rect == o.rect && min_z == o.min_z && max_z == o.max_z;
        }
    };
    // shim::Device::draw, for a draw into the game picture that writes depth: the query for its probe copy, or invalid
    // (no relief texture, off the centre ray, the frame's probe draws used up, or another view setup than the frame's
    // first probe draw, which is logged once).
    bgfx::OcclusionQueryHandle candidate(const NormalMapEntry *entry, const float view[16], const float proj[16],
                                         const float center[3], float radius, const Target &target);
    // The scene of this frame is complete and `last_view` is its last view (Device::scene_complete, else Present):
    // kView runs right after it. True when the probe drew this frame (the scene's depth must survive its last view).
    bool place(bgfx::ViewId last_view);
    bool placed() const { return placed_; }
    // Before bgfx::frame (Device::Present): an unused kView goes back to its place.
    void before_frame();
    // After the frame (Host::end_frame): reads the queries of kBatches frames ago, updates the target and logs it.
    void end_frame(bool scene_frame, NormalMaps &normal_maps);

    const render::ParallaxProbeState &state() const { return state_; }
    void shutdown();
    void on_device_lost();

  private:
    struct Draw
    {
        std::string name;
        const char *relief_class = "";
    };
    struct Batch
    {
        std::vector<Draw> draws; // the query of draws[i] is queries_[batch * kPerFrame + i]
        bool valid = false;      // submitted in a scene frame and placed after its scene
    };
    void release();
    void refresh(NormalMaps &normal_maps, bool log);
    bool enabled_ = false;
    static constexpr int kGiveUp = 120; // frames with probe draws whose queries never came back
    int inconclusive_ = 0;
    bool overflow_logged_ = false;
    std::vector<bgfx::OcclusionQueryHandle> queries_; // kBatches * kPerFrame, while enabled
    std::array<Batch, kBatches> batches_{};
    int current_ = 0;
    bool placed_ = false, remapped_ = false;
    Target target_{};
    bool view_set_ = false; // kView has this frame's target
    bool mismatch_logged_ = false;
    std::string target_class_;
    uint32_t refresh_frames_ = 0;
    render::ParallaxProbeState state_;
};

} // namespace storm::effects
