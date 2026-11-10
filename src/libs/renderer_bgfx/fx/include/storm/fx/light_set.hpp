// Point and spot lights for the per-pixel lighting path: the lights the
// fixed-function pipeline receives through SetLight / LightEnable (location
// torches and lanterns, ship lanterns, cannon flashes), plus short-lived flash
// lights for shots and explosions. Selection of the few lights that matter
// for one draw call happens here, on the CPU.
#pragma once

#include "storm/fx/sky_model.hpp"

#include <array>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr int kMaxPointLights = 8; // per draw call; the fixed-function limit of the engine

// One D3DLIGHT9 of type POINT or SPOT, world space. `color` is D3DLIGHT9.Diffuse with whatever flicker or
// fade the engine applied this frame.
struct PointLight
{
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float range = 10.0f; // D3DLIGHT9.Range: no light beyond it
    float att0 = 0.0f;   // D3DLIGHT9.Attenuation0..2: 1 / (att0 + att1 * d + att2 * d²)
    float att1 = 0.0f;
    float att2 = 1.0f;
    // Spot lights only (D3DLIGHT_SPOT); a point light keeps cos_outer = -1.
    Vec3 direction{0.0f, -1.0f, 0.0f}; // unit, the way the light points
    float cos_inner = 1.0f;            // cos(Theta / 2)
    float cos_outer = -1.0f;           // cos(Phi / 2)
    float falloff = 1.0f;              // D3DLIGHT9.Falloff
    // True when the light is already in the baked vertex colours of static location geometry (the lighter
    // tool's output). Such lights must not be added again to geometry drawn with those colours.
    bool baked = false;
};

// Fixed-function attenuation of `light` at `distance`, including the range cut-off and, at the very end of the
// range, the short fade that hides the cut-off edge per pixel (mirrors fx_lighting.sh).
float LightAttenuation(const PointLight &light, float distance);

struct LightSelection
{
    std::array<PointLight, kMaxPointLights> lights{};
    int count = 0;
    int dropped = 0; // lights that reach the object but did not fit into the budget (the weakest ones)
};

// The `max_count` lights that contribute most to an object with the given bounding sphere, strongest first.
// Lights that cannot reach the sphere are dropped. `include_baked = false` is for geometry whose vertex
// colours already contain the baked lights: only dynamic lights (flashes, carried lanterns) are returned.
LightSelection SelectLights(std::span<const PointLight> lights, Vec3 center, float radius, int max_count, bool include_baked = true);

// Short-lived lights for muzzle flashes and explosions. Add() at the event, Update() once per frame,
// then append Lights() to the frame's light list.
class FlashLights
{
  public:
    // `peak_color` may exceed 1; the light fades to nothing over `duration` seconds (fast start, long tail).
    void Add(Vec3 position, Vec3 peak_color, float range, float duration);
    void Update(float delta_seconds);
    void AppendTo(std::vector<PointLight> &lights) const;
    size_t Count() const
    {
        return flashes_.size();
    }

    static constexpr size_t kCapacity = 32; // the oldest flash is replaced when full

  private:
    struct Flash
    {
        Vec3 position;
        Vec3 color;
        float range;
        float duration;
        float age;
    };
    std::vector<Flash> flashes_;
};

// Uniform block u_fxPoint / u_fxPointCount of fx_lighting.sh.
struct PointLightUniforms
{
    std::array<std::array<float, 4>, kMaxPointLights * 4> u_fxPoint{};
    std::array<float, 4> u_fxPointCount{};
    int dropped = 0; // LightSelection::dropped, for Lighting's statistics
};

// `view` is the world -> view matrix (row-vector convention, as D3DTS_VIEW / bx).
PointLightUniforms PackPointLights(const LightSelection &selection, const std::array<float, 16> &view);

} // namespace storm::fx
