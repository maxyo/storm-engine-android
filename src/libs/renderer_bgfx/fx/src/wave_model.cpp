#include "storm/fx/wave_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace storm::fx
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr int kMask = EngineWaveField::kSize - 1;
} // namespace

EngineWaveField::EngineWaveField()
{
    for (int layer = 0; layer < 2; ++layer)
    {
        heights_[layer].assign(kTexels, 0.0f);
        normals_[layer].assign(2 * kTexels, 0.0f);
    }
}

void EngineWaveField::SetLayer(int layer, std::span<const float> heights, std::span<const float> normals_xz)
{
    std::copy_n(heights.begin(), std::min<size_t>(heights.size(), kTexels), heights_[layer].begin());
    std::copy_n(normals_xz.begin(), std::min<size_t>(normals_xz.size(), 2 * kTexels), normals_[layer].begin());
}

EngineWaveField::Sample EngineWaveField::SampleLayer(int layer, float u, float v) const
{
    const float fu = std::floor(u), fv = std::floor(v);
    const float tx = u - fu, tz = v - fv;
    const int x0 = static_cast<int>(fu) & kMask, x1 = (x0 + 1) & kMask;
    const int z0 = static_cast<int>(fv) & kMask, z1 = (z0 + 1) & kMask;
    // a1 + fX * (a2 - a1) + fZ * (a3 - a1) + fX * fZ * (a4 + a1 - a2 - a3), as in sea.cpp
    const auto bilinear = [&](const float *data, int stride, int channel) {
        const float a1 = data[stride * (x0 + z0 * kSize) + channel];
        const float a2 = data[stride * (x1 + z0 * kSize) + channel];
        const float a3 = data[stride * (x0 + z1 * kSize) + channel];
        const float a4 = data[stride * (x1 + z1 * kSize) + channel];
        return a1 + tx * (a2 - a1) + tz * (a3 - a1) + tx * tz * (a4 + a1 - a2 - a3);
    };
    return Sample{bilinear(heights_[layer].data(), 1, 0), bilinear(normals_[layer].data(), 2, 0),
                  bilinear(normals_[layer].data(), 2, 1)};
}

float EngineWaveField::Height(const EngineSeaState &state, float x, float z, float *normal) const
{
    const EngineSeaLayer &l1 = state.layers[0];
    const EngineSeaLayer &l2 = state.layers[1];
    const Sample s1 = SampleLayer(0, (x + l1.move[0]) * l1.scale, (z + l1.move[1]) * l1.scale);
    const Sample s2 = SampleLayer(1, (x + l2.move[0]) * l2.scale, (z + l2.move[1]) * l2.scale);
    if (normal)
    {
        const float ny1 = std::sqrt(std::max(1.0f - (s1.nx * s1.nx + s1.nz * s1.nz), 0.0f));
        const float ny2 = std::sqrt(std::max(1.0f - (s2.nx * s2.nx + s2.nz * s2.nz), 0.0f));
        const float nx = l1.scale * l1.amp * s1.nx + l2.scale * l2.amp * s2.nx;
        const float nz = l1.scale * l1.amp * s1.nz + l2.scale * l2.amp * s2.nz;
        const float ny = ny1 + ny2;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        normal[0] = nx / len;
        normal[1] = ny / len;
        normal[2] = nz / len;
    }
    return l1.amp * s1.height + l2.amp * s2.height;
}

SeaVertex EngineWaveField::Displace(const EngineSeaState &state, float x, float z, std::array<float, 2> world_offset) const
{
    SeaVertex v{};
    v.pos[1] = Height(state, x, z, v.normal);
    v.uv[0] = (x - v.normal[0] * 5.3f) * state.bump_scale;
    v.uv[1] = (z - v.normal[2] * 5.3f) * state.bump_scale;
    v.pos[0] = x - (v.normal[0] * state.pos_shift * 3.0f + world_offset[0]);
    v.pos[2] = z - (v.normal[2] * state.pos_shift * 3.0f + world_offset[1]);
    return v;
}

std::vector<uint16_t> EngineWaveField::PackLayerHalf(int layer) const
{
    std::vector<float> mip(static_cast<size_t>(kTexels) * 3);
    for (int i = 0; i < kTexels; ++i)
    {
        mip[3 * i + 0] = heights_[layer][i];
        mip[3 * i + 1] = normals_[layer][2 * i + 0];
        mip[3 * i + 2] = normals_[layer][2 * i + 1];
    }
    std::vector<uint16_t> out;
    out.reserve(static_cast<size_t>(kTexels) * 4 * 4 / 3 + 4);
    for (int size = kSize;; size /= 2)
    {
        for (int i = 0; i < size * size; ++i)
        {
            out.push_back(FloatToHalf(mip[3 * i + 0]));
            out.push_back(FloatToHalf(mip[3 * i + 1]));
            out.push_back(FloatToHalf(mip[3 * i + 2]));
            out.push_back(0);
        }
        if (size == 1)
            break;
        const int half = size / 2;
        std::vector<float> next(static_cast<size_t>(half) * half * 3);
        for (int y = 0; y < half; ++y)
            for (int x = 0; x < half; ++x)
                for (int c = 0; c < 3; ++c)
                    next[3 * (y * half + x) + c] =
                        0.25f * (mip[3 * (2 * y * size + 2 * x) + c] + mip[3 * (2 * y * size + 2 * x + 1) + c] +
                                 mip[3 * ((2 * y + 1) * size + 2 * x) + c] + mip[3 * ((2 * y + 1) * size + 2 * x + 1) + c]);
        mip = std::move(next);
    }
    return out;
}

uint16_t FloatToHalf(float value)
{
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;
    if (exponent >= 31) // overflow, infinity or NaN
        return static_cast<uint16_t>(sign | 0x7c00u | (((bits >> 23) & 0xffu) == 0xffu && mantissa ? 0x200u : 0u));
    if (exponent <= 0)
    {
        if (exponent < -10)
            return static_cast<uint16_t>(sign);
        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        const uint32_t half = mantissa >> shift;
        const uint32_t round = (mantissa >> (shift - 1)) & 1u;
        return static_cast<uint16_t>(sign | (half + round));
    }
    const uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    return static_cast<uint16_t>(half + ((mantissa >> 12) & 1u)); // round to nearest; a carry correctly bumps the exponent
}

float HalfToFloat(uint16_t value)
{
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
    const uint32_t exponent = (value >> 10) & 0x1fu;
    const uint32_t mantissa = value & 0x3ffu;
    if (exponent == 0)
    {
        const float v = std::ldexp(static_cast<float>(mantissa), -24);
        return sign ? -v : v;
    }
    if (exponent == 31)
        return std::bit_cast<float>(sign | 0x7f800000u | (mantissa << 13));
    return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
}

GerstnerSet GerstnerFromEngine(const GerstnerSource &source)
{
    GerstnerSet set;
    set.mean_level = 0.5f * (source.amp[0] + source.amp[1]);
    // Per layer: three waves at fractions of the tile length, fanned around the scroll direction.
    constexpr std::array<float, 3> kLengthDivisor{2.0f, 3.24f, 5.2f};
    constexpr std::array<float, 3> kAmplitudeShare{0.56f, 0.29f, 0.15f};
    constexpr std::array<float, 3> kSpread{0.0f, 0.42f, -0.61f}; // radians
    float steepness_sum = 0.0f;
    for (int layer = 0; layer < 2; ++layer)
    {
        const float tile = static_cast<float>(EngineWaveField::kSize) / std::max(source.scale[layer], 1e-3f);
        const float speed = std::hypot(source.move_speed[layer][0], source.move_speed[layer][1]);
        // The engine adds `move` to the sample position, so the pattern travels against the scroll vector.
        const float heading = speed > 1e-4f ? std::atan2(-source.move_speed[layer][1], -source.move_speed[layer][0])
                                            : (layer == 0 ? 0.0f : 0.5f * kPi);
        for (int i = 0; i < 3; ++i)
        {
            GerstnerWave &w = set.waves[layer * 3 + i];
            const float length = tile / kLengthDivisor[i];
            const float angle = heading + kSpread[i];
            w.dir = {std::cos(angle), std::sin(angle)};
            w.wave_number = 2.0f * kPi / length;
            // The layer spans 0..amp peak to peak; a sum of three sines reaches the full range only rarely.
            w.amplitude = 0.5f * source.amp[layer] * kAmplitudeShare[i];
            // Deep-water dispersion when the layer does not scroll, otherwise the scroll speed (longer waves faster).
            const float phase_speed = speed > 1e-4f ? speed * std::sqrt(kLengthDivisor[0] / kLengthDivisor[i])
                                                    : std::sqrt(9.81f / w.wave_number);
            w.omega = phase_speed * w.wave_number;
            w.phase = 1.7f * static_cast<float>(layer * 3 + i);
            // Engine: horizontal shift = 3 * PosShift * slope; slope amplitude of a sine is k * A.
            w.horizontal = 3.0f * source.pos_shift * w.wave_number * w.amplitude;
            steepness_sum += w.wave_number * w.horizontal;
        }
    }
    // Keep the surface free of loops: sum(k * horizontal) < 1.
    constexpr float kMaxSteepness = 0.8f;
    if (steepness_sum > kMaxSteepness)
        for (GerstnerWave &w : set.waves)
            w.horizontal *= kMaxSteepness / steepness_sum;
    return set;
}

void EvaluateGerstner(const GerstnerSet &set, float x, float z, float time, float *position, float *normal)
{
    float px = x, py = set.mean_level, pz = z;
    // Partial derivatives of the displaced position with respect to x and z.
    float dxdx = 1.0f, dxdz = 0.0f, dzdz = 1.0f, dydx = 0.0f, dydz = 0.0f;
    for (const GerstnerWave &w : set.waves)
    {
        const float theta = w.wave_number * (w.dir[0] * x + w.dir[1] * z) - w.omega * time + w.phase;
        const float s = std::sin(theta), c = std::cos(theta);
        px += w.horizontal * w.dir[0] * c;
        pz += w.horizontal * w.dir[1] * c;
        py += w.amplitude * s;
        const float kh = w.wave_number * w.horizontal, ka = w.wave_number * w.amplitude;
        dxdx -= kh * w.dir[0] * w.dir[0] * s;
        dxdz -= kh * w.dir[0] * w.dir[1] * s;
        dzdz -= kh * w.dir[1] * w.dir[1] * s;
        dydx += ka * w.dir[0] * c;
        dydz += ka * w.dir[1] * c;
    }
    if (position)
    {
        position[0] = px;
        position[1] = py;
        position[2] = pz;
    }
    if (normal)
    {
        // n = dP/dz x dP/dx (y up), with dP/dx = (dxdx, dydx, dxdz) and dP/dz = (dxdz, dydz, dzdz).
        const float nx = dydz * dxdz - dzdz * dydx;
        const float ny = dzdz * dxdx - dxdz * dxdz;
        const float nz = dxdz * dydx - dydz * dxdx;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        normal[0] = nx / len;
        normal[1] = ny / len;
        normal[2] = nz / len;
    }
}

} // namespace storm::fx
