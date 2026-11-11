#include "storm/pbr/environment.hpp"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>

namespace storm::pbr
{
namespace
{
bx::DefaultAllocator &Allocator()
{
    static bx::DefaultAllocator allocator;
    return allocator;
}

struct ImageDeleter
{
    void operator()(bimg::ImageContainer *image) const { bimg::imageFree(image); }
};
using Image = std::unique_ptr<bimg::ImageContainer, ImageDeleter>;

std::string Lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

Vec3 Normalize(Vec3 v)
{
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return {v[0] / length, v[1] / length, v[2] / length};
}

// Box-filters an RGBA32F strip by an integer factor (the cmft strips are 256..1024 per face).
std::vector<float> Downsample(const float *rgba, uint32_t width, uint32_t height, uint32_t factor)
{
    const uint32_t w = width / factor, h = height / factor;
    std::vector<float> out(size_t(w) * h * 4, 0.0f);
    const float weight = 1.0f / float(factor * factor);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            float *dst = &out[(size_t(y) * w + x) * 4];
            for (uint32_t sy = 0; sy < factor; ++sy)
                for (uint32_t sx = 0; sx < factor; ++sx)
                {
                    const float *src = &rgba[((size_t(y) * factor + sy) * width + x * factor + sx) * 4];
                    for (int c = 0; c < 4; ++c)
                        dst[c] += src[c] * weight;
                }
        }
    return out;
}

// Real SH basis, band 0..2 (Ramamoorthi & Hanrahan 2001, eq. 3).
std::array<float, 9> ShBasis(const Vec3 &d)
{
    const float x = d[0], y = d[1], z = d[2];
    return {0.282095f,
            0.488603f * y,
            0.488603f * z,
            0.488603f * x,
            1.092548f * x * y,
            1.092548f * y * z,
            0.315392f * (3.0f * z * z - 1.0f),
            1.092548f * x * z,
            0.546274f * (x * x - y * y)};
}

float TexelSolidAngle(float u, float v, float inv_size)
{
    const auto area = [](float x, float y) { return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f)); };
    const float x0 = u - inv_size, x1 = u + inv_size, y0 = v - inv_size, y1 = v + inv_size;
    return area(x1, y1) - area(x0, y1) - area(x1, y0) + area(x0, y0);
}
} // namespace

size_t Cube::MipOffset(uint32_t mip, uint32_t face) const
{
    size_t offset = 0;
    for (uint32_t m = 0; m < mip; ++m)
        offset += size_t(MipSize(m)) * MipSize(m) * 4 * 6;
    return offset + size_t(face) * MipSize(mip) * MipSize(mip) * 4;
}

Vec3 CubeTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size)
{
    const float u = (2.0f * (float(x) + 0.5f) / float(size)) - 1.0f;
    const float v = (2.0f * (float(y) + 0.5f) / float(size)) - 1.0f;
    switch (face)
    {
    case 0: return Normalize({1.0f, -v, -u});
    case 1: return Normalize({-1.0f, -v, u});
    case 2: return Normalize({u, 1.0f, v});
    case 3: return Normalize({u, -1.0f, -v});
    case 4: return Normalize({u, -v, 1.0f});
    default: return Normalize({-u, -v, -1.0f});
    }
}

bool BakeEnvironmentFromStrip(const float *rgba, uint32_t width, uint32_t height, const BakeOptions &options, Environment &out,
                              std::string &error)
{
    const bool horizontal = width == height * 6;
    if (!horizontal && height != width * 6)
    {
        error = "not a cube strip (" + std::to_string(width) + "x" + std::to_string(height) + ", expected 6:1 or 1:6)";
        return false;
    }
    const uint32_t face = horizontal ? height : width;
    if (options.face_size == 0 || face < options.face_size || face % options.face_size != 0 ||
        (options.face_size & (options.face_size - 1)) != 0)
    {
        error = "face size " + std::to_string(face) + " cannot be reduced to " + std::to_string(options.face_size);
        return false;
    }
    const uint32_t factor = face / options.face_size;
    std::vector<float> reduced;
    const float *strip = rgba;
    uint32_t w = width, h = height;
    if (factor > 1)
    {
        reduced = Downsample(rgba, width, height, factor);
        strip = reduced.data();
        w /= factor;
        h /= factor;
    }

    bx::Error err;
    bimg::ImageContainer input{};
    input.m_data = const_cast<float *>(strip);
    input.m_size = w * h * 16;
    input.m_width = w;
    input.m_height = h;
    input.m_depth = 1;
    input.m_numLayers = 1;
    input.m_format = bimg::TextureFormat::RGBA32F;
    input.m_orientation = bimg::Orientation::R0;
    input.m_numMips = 1;
    const Image cube(bimg::imageCubemapFromStripRgba32F(&Allocator(), input, &err));
    if (!cube || !err.isOk())
    {
        error = "strip to cube failed: " + std::string(err.getMessage().getCPtr(), err.getMessage().getLength());
        return false;
    }
    const Image radiance(bimg::imageCubemapRadianceFilter(&Allocator(), *cube, bimg::LightingModel::Ggx, &err));
    if (!radiance || !err.isOk())
    {
        error = "radiance filter failed: " + std::string(err.getMessage().getCPtr(), err.getMessage().getLength());
        return false;
    }

    Cube &result = out.radiance;
    result.size = radiance->m_width;
    result.mips = radiance->m_numMips;
    result.rgba.assign(result.MipOffset(result.mips, 0), 0.0f);
    for (uint32_t mip = 0; mip < result.mips; ++mip)
        for (uint8_t side = 0; side < 6; ++side)
        {
            bimg::ImageMip level;
            bimg::imageGetRawData(*radiance, side, uint8_t(mip), radiance->m_data, radiance->m_size, level);
            const uint32_t n = result.MipSize(mip);
            if (level.m_width != n || level.m_format != bimg::TextureFormat::RGBA32F)
            {
                error = "radiance filter returned an unexpected layout";
                return false;
            }
            std::memcpy(&result.rgba[result.MipOffset(mip, side)], level.m_data, size_t(n) * n * 16);
        }
    // Irradiance from the unfiltered mip 0 (mip 0 of the radiance cube is the input itself).
    out.irradiance = ProjectIrradianceSh9(result, 0);
    return true;
}

bool BakeEnvironment(std::span<const uint8_t> hdr_file, const BakeOptions &options, Environment &out, std::string &error)
{
    bx::Error err;
    const Image image(bimg::imageParse(&Allocator(), hdr_file.data(), uint32_t(hdr_file.size()), bimg::TextureFormat::RGBA32F, &err));
    if (!image || !err.isOk())
    {
        error = "cannot decode the Radiance HDR file";
        if (!err.isOk())
            error += ": " + std::string(err.getMessage().getCPtr(), err.getMessage().getLength());
        return false;
    }
    if (image->m_format != bimg::TextureFormat::RGBA32F || image->m_cubeMap || image->m_depth > 1)
    {
        error = "decoded HDR is not a 2D RGBA32F image";
        return false;
    }
    return BakeEnvironmentFromStrip(static_cast<const float *>(image->m_data), image->m_width, image->m_height, options, out, error);
}

Sh9 ProjectIrradianceSh9(const Cube &cube, uint32_t mip)
{
    // Radiance coefficients L_lm (solid-angle weighted), then irradiance E_lm = A_l L_lm with A = (pi, 2pi/3, pi/4)
    // (Ramamoorthi & Hanrahan 2001, eq. 8), divided by pi.
    std::array<double, 27> sum{};
    double total_weight = 0.0;
    const uint32_t n = cube.MipSize(mip);
    const float inv = 1.0f / float(n);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const float *texels = &cube.rgba[cube.MipOffset(mip, face)];
        for (uint32_t y = 0; y < n; ++y)
            for (uint32_t x = 0; x < n; ++x)
            {
                const float u = 2.0f * (float(x) + 0.5f) * inv - 1.0f;
                const float v = 2.0f * (float(y) + 0.5f) * inv - 1.0f;
                const float weight = TexelSolidAngle(u, v, inv);
                const std::array<float, 9> basis = ShBasis(CubeTexelDirection(face, x, y, n));
                const float *c = &texels[(size_t(y) * n + x) * 4];
                for (int k = 0; k < 9; ++k)
                    for (int ch = 0; ch < 3; ++ch)
                        sum[size_t(k * 3 + ch)] += double(c[ch]) * basis[size_t(k)] * weight;
                total_weight += weight;
            }
    }
    // Normalise the solid angle to exactly 4 pi (texel areas sum to it up to rounding).
    const double norm = 4.0 * std::numbers::pi / total_weight;
    static constexpr double band[9] = {1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25};
    Sh9 sh;
    for (int k = 0; k < 9; ++k)
        for (int ch = 0; ch < 3; ++ch)
            sh.c[size_t(k)][size_t(ch)] = float(sum[size_t(k * 3 + ch)] * norm * band[k]);
    return sh;
}

Vec3 EvaluateIrradiance(const Sh9 &sh, const Vec3 &normal)
{
    const std::array<float, 9> basis = ShBasis(Normalize(normal));
    Vec3 out{};
    for (int k = 0; k < 9; ++k)
        for (int ch = 0; ch < 3; ++ch)
            out[size_t(ch)] += sh.c[size_t(k)][size_t(ch)] * basis[size_t(k)];
    return out;
}

std::vector<float> BrdfLut(uint32_t size)
{
    // Karis 2013: integrate the GGX specular lobe with Smith-Schlick visibility (k = a/2 for IBL) and Schlick
    // Fresnel split into F0 * scale + bias; Hammersley points, importance sampled GGX.
    constexpr uint32_t kSamples = 512;
    std::vector<float> out(size_t(size) * size * 2);
    for (uint32_t row = 0; row < size; ++row)
    {
        const float roughness = (float(row) + 0.5f) / float(size);
        const float a = roughness * roughness;
        const float k = a / 2.0f;
        for (uint32_t col = 0; col < size; ++col)
        {
            const float nov = (float(col) + 0.5f) / float(size);
            const Vec3 view{std::sqrt(1.0f - nov * nov), 0.0f, nov};
            double scale = 0.0, bias = 0.0;
            for (uint32_t i = 0; i < kSamples; ++i)
            {
                uint32_t bits = i;
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                const float e1 = float(i) / float(kSamples), e2 = float(bits) * 2.3283064365386963e-10f;
                const float phi = 2.0f * std::numbers::pi_v<float> * e1;
                const float cos_theta = std::sqrt((1.0f - e2) / (1.0f + (a * a - 1.0f) * e2));
                const float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);
                const Vec3 h{sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
                const float voh = view[0] * h[0] + view[1] * h[1] + view[2] * h[2];
                const float nol = 2.0f * voh * h[2] - view[2];
                const float noh = h[2];
                if (nol <= 0.0f)
                    continue;
                const float g1v = nov / (nov * (1.0f - k) + k);
                const float g1l = nol / (nol * (1.0f - k) + k);
                const float g_vis = g1v * g1l * std::max(voh, 0.0f) / (noh * nov);
                const float fc = std::pow(1.0f - std::max(voh, 0.0f), 5.0f);
                scale += (1.0f - fc) * g_vis;
                bias += fc * g_vis;
            }
            out[(size_t(row) * size + col) * 2 + 0] = float(scale / kSamples);
            out[(size_t(row) * size + col) * 2 + 1] = float(bias / kSamples);
        }
    }
    return out;
}

bool ToGrade(const scene_data::ToneMapping &tone, Grade &out, std::string &error)
{
    if (tone.type != "aces")
    {
        error = "tone mapping type \"" + tone.type + "\" is not implemented (only \"aces\")";
        return false;
    }
    if (tone.temperature != 0.0f || tone.tint != 0.0f)
    {
        error = "white balance (temperature / tint) is not implemented";
        return false;
    }
    const float scale = std::exp2(tone.exposure_ev);
    out.scale = {tone.color_filter[0] * scale, tone.color_filter[1] * scale, tone.color_filter[2] * scale};
    out.contrast = tone.contrast;
    out.saturation = tone.saturation;
    out.lift = tone.lift;
    out.gamma = tone.gamma;
    out.gain = tone.gain;
    return true;
}

Vec3 SunDirection(float azimuth_degrees, float height_degrees)
{
    const float az = azimuth_degrees * std::numbers::pi_v<float> / 180.0f;
    const float el = height_degrees * std::numbers::pi_v<float> / 180.0f;
    return Normalize({std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)});
}

bool HourConstants(const scene_data::LightingPreset &preset, int hour, HourLighting &out, std::string &error)
{
    if (hour < 0 || hour > 23 || !preset.hours[size_t(hour)])
    {
        error = "preset \"" + preset.preset_name + "\" has no hour " + std::to_string(hour);
        return false;
    }
    const scene_data::LightingHour &h = *preset.hours[size_t(hour)];
    out = HourLighting{};
    for (int c = 0; c < 3; ++c)
    {
        out.sun[size_t(c)] = h.sun_color[size_t(c)] * h.sun_intensity;
        out.ambient[size_t(c)] = h.ambient_color[size_t(c)] * h.ambient_intensity;
    }
    out.exposure = h.exposure;
    out.emission = h.emission;
    out.hdr_texture = h.hdr_texture;
    for (const auto &[group, light] : h.point_lights)
        out.point_groups.emplace_back(Lower(group), Vec3{light.color[0] * light.intensity, light.color[1] * light.intensity,
                                                         light.color[2] * light.intensity});
    if (preset.sun_azimuth && preset.sun_height)
    {
        out.sun_direction = SunDirection(*preset.sun_azimuth, *preset.sun_height);
        out.sun_direction_known = true;
    }
    return true;
}

bool BlendedHourConstants(const scene_data::LightingPreset &preset, float hour, HourLighting &out, std::string &error)
{
    if (!std::isfinite(hour) || hour < 0.0f || hour >= 24.0f)
    {
        error = "hour " + std::to_string(hour) + " outside 0..24";
        return false;
    }
    const int h0 = int(hour), h1 = (h0 + 1) % 24;
    const float t = hour - float(h0);
    HourLighting next;
    if (!HourConstants(preset, h0, out, error) || !HourConstants(preset, h1, next, error))
        return false;
    const auto mix = [t](float a, float b) { return a + (b - a) * t; };
    for (size_t c = 0; c < 3; ++c)
    {
        out.sun[c] = mix(out.sun[c], next.sun[c]);
        out.ambient[c] = mix(out.ambient[c], next.ambient[c]);
    }
    out.exposure = mix(out.exposure, next.exposure);
    out.emission = mix(out.emission, next.emission);
    for (auto &[group, color] : out.point_groups)
        for (const auto &[other, other_color] : next.point_groups)
            if (other == group)
                for (size_t c = 0; c < 3; ++c)
                    color[c] = mix(color[c], other_color[c]);
    return true;
}

std::vector<std::string> LightingLibrary::Add(std::string file, scene_data::LightingPreset preset)
{
    std::vector<std::string> errors;
    const size_t index = presets_.size();
    for (const std::string &id : preset.location_ids)
    {
        if (id.empty())
            continue;
        std::string key = Lower(id);
        const auto existing = std::ranges::find(ids_, key, &std::pair<std::string, size_t>::first);
        if (existing != ids_.end())
        {
            // A preset may list the same id twice; that entry points at this preset, which is not stored yet.
            if (existing->second == index)
                errors.push_back("location \"" + id + "\" is listed twice in " + file);
            else
                errors.push_back("location \"" + id + "\" is claimed by " + presets_[existing->second].file + " and " +
                                 file + " (keeping the first)");
            continue;
        }
        ids_.emplace_back(std::move(key), index);
    }
    presets_.push_back({std::move(file), std::move(preset)});
    return errors;
}

const scene_data::LightingPreset *LightingLibrary::Find(std::string_view location_id) const
{
    const std::string key = Lower(location_id);
    const auto it = std::ranges::find(ids_, key, &std::pair<std::string, size_t>::first);
    return it == ids_.end() ? nullptr : &presets_[it->second].preset;
}

const std::string *LightingLibrary::FileOf(std::string_view location_id) const
{
    const std::string key = Lower(location_id);
    const auto it = std::ranges::find(ids_, key, &std::pair<std::string, size_t>::first);
    return it == ids_.end() ? nullptr : &presets_[it->second].file;
}

} // namespace storm::pbr
