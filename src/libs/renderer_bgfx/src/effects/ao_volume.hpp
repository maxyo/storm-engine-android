// The ambient-occlusion volume of the current interior (fx/ao_volume.hpp, Settings::ao_volume, option fx_ao_volume).
//
// The location reports the static geometry of an interior once it is loaded (render::SetEffectsAoGeometry, from
// Location's first frame). While the settings ask for the volume, it is read from the texture cache directory or
// baked on a texture worker (fx::BakeAoVolume, about a second for an interior, the time logged) and
// uploaded as an R8 3D texture; the lit and baked fixed-function variants sample it. Outdoors, or before the volume is
// ready, there is none and nothing darkens. A failed bake or upload is logged once per location.
#pragma once

#include "storm/fx/ao_volume.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace storm::effects
{

class AoVolumes
{
  public:
    struct Bound
    {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        std::array<float, 16> world_to_uvw{}; // row-vector world -> 0..1 texture coordinates
        float cell = 0.0f;                     // metres: the receivers' normal offset is half of it
    };

    // Main thread. Replaces the location's geometry (empty: outdoors or no location, the volume goes).
    // `bounds_lo` > `bounds_hi` (or both zero): no bounds, the geometry's box.
    void set_geometry(std::string name, std::vector<fx::Vec3> positions, std::vector<uint32_t> indices,
                      const fx::Vec3 &bounds_lo, const fx::Vec3 &bounds_hi);
    // Main thread, once per frame with the active settings: starts the load or bake when wanted, uploads a finished
    // one. The volume to sample this frame, or nullptr.
    const Bound *update(bool wanted);

    void shutdown();
    void on_device_lost();

  private:
    struct Job
    {
        enum State : int
        {
            Pending,
            Ready,
            Failed,
        };
        std::atomic<int> state{Pending};
        std::string name;
        fx::AoVolume volume;       // written by the worker before `state` becomes Ready
        std::string how;           // "cache" or "baked in ... ms": for the log
    };
    void start();
    void destroy_texture();

    std::string name_;
    std::vector<fx::Vec3> positions_;
    std::vector<uint32_t> indices_;
    fx::Vec3 bounds_lo_{1.0f, 1.0f, 1.0f}, bounds_hi_{-1.0f, -1.0f, -1.0f};
    std::shared_ptr<Job> job_;
    bool failure_reported_ = false;
    Bound bound_{};
};

} // namespace storm::effects
