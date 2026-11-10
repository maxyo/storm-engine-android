// CPU side of the water: the engine's own wave model (src/libs/sea) restated
// without engine types, and its mapping onto a small set of Gerstner waves.
//
// The SEA entity animates two layers of a tiling 128x128 height field. Each
// frame it keeps, per layer, the current heights (pSeaFrame1/2, 0..1) and the
// x/z components of unit normals (pSeaNormalsFrame1/2). SEA::WaveXZ samples
// both layers bilinearly; ships float on that function, so anything drawn as
// "the sea" has to follow it. `EngineWaveField` reproduces it bit for bit in
// spirit (same formulas, same order) and `Water` evaluates the same field on
// the GPU from the two layer textures.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

struct EngineSeaLayer
{
    float amp = 0.0f;                    // fAmp1 / fAmp2 (already limited by MaxSeaHeight)
    float scale = 1.0f;                  // fScale1 / fScale2: texels per metre
    std::array<float, 2> move{0.0f, 0.0f}; // vMove1.x, vMove1.z: accumulated scroll in metres
};

struct EngineSeaState
{
    std::array<EngineSeaLayer, 2> layers;
    float pos_shift = 1.2f;  // fPosShift: horizontal "sharpening" of the crests
    float bump_scale = 0.1f; // fBumpScale: detail texture coordinates per metre
};

// The vertex the engine writes into its sea vertex buffer (SEA::SeaVertex).
struct SeaVertex
{
    float pos[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(SeaVertex) == 32);

class EngineWaveField
{
  public:
    static constexpr int kSize = 128;
    static constexpr int kTexels = kSize * kSize;

    EngineWaveField();

    // heights: kTexels values (pSeaFrameN); normals_xz: 2 * kTexels values (pSeaNormalsFrameN).
    void SetLayer(int layer, std::span<const float> heights, std::span<const float> normals_xz);

    // SEA::WaveXZ without the far-distance cut-off. `normal` receives the unit normal when not null.
    float Height(const EngineSeaState &state, float x, float z, float *normal = nullptr) const;

    // One grid vertex as SEA::SSE_WaveXZBlock builds it. (x, z) is the undisplaced world position;
    // the result is relative to `world_offset` (the engine uses the camera x/z).
    SeaVertex Displace(const EngineSeaState &state, float x, float z, std::array<float, 2> world_offset = {0.0f, 0.0f}) const;

    // Texture for Water's height-field vertex path: RGBA16F texels (height 0..1, normal x, normal z, 0),
    // row = z, column = x; the full box-filtered mip chain 128, 64 .. 1, top first. Coarse grid levels
    // read the smaller mips, so distant water flattens instead of aliasing.
    static constexpr int kMipCount = 8;
    std::vector<uint16_t> PackLayerHalf(int layer) const;

  private:
    struct Sample
    {
        float height, nx, nz;
    };
    Sample SampleLayer(int layer, float u, float v) const;

    std::array<std::vector<float>, 2> heights_;
    std::array<std::vector<float>, 2> normals_;
};

// IEEE 754 binary16 conversion used for the layer textures.
uint16_t FloatToHalf(float value);
float HalfToFloat(uint16_t value);

// ---- Gerstner approximation ----
// For views that have no SEA entity behind them (menus, the world map, the
// preview tool) or when the height-field sampling is to be dropped entirely.
// The set is a statistical stand-in for the two engine layers: same mean
// level, comparable height range, wave lengths and travel directions.
struct GerstnerWave
{
    std::array<float, 2> dir{1.0f, 0.0f}; // unit travel direction (x, z)
    float wave_number = 0.0f;             // 2*pi / wave length
    float amplitude = 0.0f;               // metres
    float horizontal = 0.0f;              // horizontal displacement amplitude, metres
    float omega = 0.0f;                   // rad/s
    float phase = 0.0f;                   // rad
};

inline constexpr int kGerstnerWaveCount = 6;

struct GerstnerSet
{
    std::array<GerstnerWave, kGerstnerWaveCount> waves;
    float mean_level = 0.0f; // metres above y = 0
};

struct GerstnerSource
{
    std::array<float, 2> amp{0.0f, 0.0f};                              // Sea2.Amp1 / Amp2
    std::array<float, 2> scale{0.5f, 2.0f};                            // Sea2.Scale1 / Scale2
    std::array<std::array<float, 2>, 2> move_speed{{{5.0f, 0.0f}, {0.0f, 2.0f}}}; // Sea2.MoveSpeed1/2 (x, z), m/s
    float pos_shift = 1.2f;                                            // Sea2.PosShift
};

GerstnerSet GerstnerFromEngine(const GerstnerSource &source);

// Displaced position and unit normal of the undisplaced point (x, z) at `time` seconds.
void EvaluateGerstner(const GerstnerSet &set, float x, float z, float time, float *position, float *normal);

} // namespace storm::fx
