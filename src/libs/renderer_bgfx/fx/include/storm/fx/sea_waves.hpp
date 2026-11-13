// Waves from objects on the legacy sea (Settings::sea_waves): a wave-equation height field on the GPU around the
// camera, stirred every step by swimmers, waders, boats and ship hulls, and read by the Sea2 shaders
// (shaders/include/sea_waves.sh) for their normals, crest foam and (Settings::sea_waves_lift) a vertex lift.
//
// The field: kSize x kSize texels over kExtent metres, centred on the camera in whole texels (the content is shifted
// by whole texels as the centre moves, so the water does not swim under the camera), fixed 60 Hz steps (at most
// kMaxSteps a frame). Each step is one screen pass that reads the previous state (height now and one step ago) and
// writes the next one: the discrete 2D wave equation (Hugo Elias' ripple scheme, the standard explicit leapfrog
// integration), with velocity damping, a sponge at the border and every source applied in the same pass. A hull
// holds the water still inside its waterline (waves reflect off it) and pushes it around the waterline with its
// speed (most at the bow) and its heave. Then a surface pass derives, per texel, the height, its gradient and a
// crest-foam amount into an RGBA8 texture that every device samples with linear filtering, in vertex and fragment
// shaders alike.
//
// The state is RG16F where it renders (Support::sea_waves_float); otherwise each height is packed into two RGBA8
// channels as 16-bit fixed point (logged once): exact under the point sampling the passes use.
#pragma once

#include "storm/fx/wave_clock.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>

namespace storm::fx
{

struct WaveSource
{
    enum class Kind : uint8_t
    {
        Body, // a swimmer or wader: a round disturbance that moves and (swimming) strokes
        Hull, // a boat or ship: an ellipse on the waterline that holds the water inside and pushes it around
    };
    Kind kind = Kind::Body;
    std::array<double, 2> position{}; // absolute world x, z (metres)
    std::array<float, 2> forward{0.0f, 1.0f}; // unit heading in x, z
    float half_length = 0.5f; // metres along `forward` (a body: its radius)
    float half_width = 0.5f;
    float speed = 0.0f;  // m/s along `forward`
    float heave = 0.0f;  // m/s up (a hull bobbing in the swell sends rings out)
    float stroke = 0.0f; // a body: 0 wading .. 1 swimming hard (an oscillating push)
};

class SeaWaves
{
  public:
    static constexpr uint16_t kSize = 256;
    static constexpr float kExtent = 128.0f; // metres the field covers
    static constexpr float kCell = kExtent / kSize;
    static constexpr int kMaxSources = 16;
    static constexpr int kMaxSteps = WaveClock::kMaxSteps;
    static constexpr float kStep = WaveClock::kStep;
    // Heights the surface texture holds: +-kSurfaceHeight metres; slopes +-kSurfaceSlope.
    static constexpr float kSurfaceHeight = 0.5f;
    static constexpr float kSurfaceSlope = 1.0f;

    SeaWaves() = default;
    SeaWaves(const SeaWaves &) = delete;
    SeaWaves &operator=(const SeaWaves &) = delete;
    ~SeaWaves();

    // Programs for the settings switch (counts against `budget` for each it has to load).
    bool Prepare(int &budget);
    // Steps due after `dt` seconds of game time (0..kMaxSteps): the caller provides that many views plus one
    // for the surface pass to Advance (none when 0).
    int StepsDue(float dt) { return clock_.Due(dt); }
    // Runs `views.size() - 1` steps, centred on `camera` (absolute world x, z), with this frame's `sources` (the
    // first kMaxSources: the caller sorts them by importance), then the surface pass in the last view.
    // `float_state`: Support::sea_waves_float.
    void Advance(std::span<const bgfx::ViewId> views, std::array<double, 2> camera, std::span<const WaveSource> sources,
                 bool float_state);
    // Drops the water's state (a new scene, a teleport): the next Advance starts from still water.
    void Reset();
    // The RGBA8 surface of the last Advance (R height, GB gradient in x / z, A crest foam; see the constants above),
    // and the absolute world x, z of its centre: uv = (world - centre) / kExtent + 0.5.
    bgfx::TextureHandle Surface() const;
    std::array<double, 2> Centre() const { return centre_; }
    bool Ready() const { return surface_ready_; }
    void Shutdown();
    void OnDeviceLost();

  private:
    void Create(bool float_state);
    bgfx::FrameBufferHandle state_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle surface_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle step_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle derive_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_state_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_step_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sources_ = BGFX_INVALID_HANDLE;
    int current_ = 0;           // state_[current_] holds the latest step
    bool float_state_ = false;  // the format state_ was created with
    bool reset_ = true;         // the next step writes still water
    bool surface_ready_ = false;
    std::array<int64_t, 2> cell_{}; // the centre in whole texels
    std::array<double, 2> centre_{};
    WaveClock clock_;
    double time_ = 0.0;    // stepped game time (the swimmers' strokes)
};

} // namespace storm::fx
