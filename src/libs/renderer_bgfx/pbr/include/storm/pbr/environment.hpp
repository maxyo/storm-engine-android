// Image-based lighting and the per-hour lighting constants of the Caribbean Legend lighting presets
// (Resource/Lighting/*.json + the hour's .hdr). CPU only, no GPU.
//
//  * BakeEnvironment: Radiance RGBE file (cmft output, horizontal 6:1 or vertical 1:6 cube strip) -> GGX
//    prefiltered radiance cube for the split-sum approximation (Karis 2013) + SH9 irradiance. Decode, strip ->
//    cube and the GGX prefilter are bimg's (imageParse, imageCubemapFromStripRgba32F,
//    imageCubemapRadianceFilter); mip i of the radiance cube holds perceptual roughness i / (mips - 1).
//  * ProjectIrradianceSh9 / EvaluateIrradiance: Ramamoorthi & Hanrahan 2001, "An Efficient Representation for
//    Irradiance Environment Maps" (cosine lobe folded into the coefficients, result divided by pi so that a
//    Lambert surface multiplies it by its albedo).
//  * BrdfLut: the split-sum environment BRDF table (Karis 2013): (scale, bias) of F0 per (N.V, roughness).
#pragma once

#include "storm/scene_data/scene_data.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace storm::pbr
{

using Vec3 = std::array<float, 3>;

// Cube map, RGBA32F, linear radiance. Layout: for each mip, for each face (+X -X +Y -Y +Z -Z), rows top to bottom.
struct Cube
{
    uint32_t size = 0; // face size of mip 0
    uint32_t mips = 0;
    std::vector<float> rgba;

    size_t MipOffset(uint32_t mip, uint32_t face) const; // in floats
    uint32_t MipSize(uint32_t mip) const { return size >> mip ? size >> mip : 1u; }
};

struct Sh9
{
    std::array<Vec3, 9> c{}; // irradiance / pi coefficients, band order (0,0) (1,-1) (1,0) (1,1) (2,-2) ... (2,2)
};

struct Environment
{
    Cube radiance; // GGX prefiltered, mips = roughness
    Sh9 irradiance;
};

struct BakeOptions
{
    uint32_t face_size = 32; // radiance cube face size (the strip is box-filtered down to it)
};

// Direction of texel centre (x, y) on `face` of a cube with faces of `size` texels (D3D / bgfx convention).
Vec3 CubeTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size);

// Bakes from the bytes of a .hdr file. Returns false with `error` set (file name not included).
bool BakeEnvironment(std::span<const uint8_t> hdr_file, const BakeOptions &options, Environment &out, std::string &error);
// Bakes from a linear RGBA32F strip (width = 6 * height or height = 6 * width), e.g. synthetic test data.
bool BakeEnvironmentFromStrip(const float *rgba, uint32_t width, uint32_t height, const BakeOptions &options, Environment &out,
                              std::string &error);

Sh9 ProjectIrradianceSh9(const Cube &cube, uint32_t mip);
Vec3 EvaluateIrradiance(const Sh9 &sh, const Vec3 &normal);

// size x size table, two floats per entry (scale, bias); row = roughness (0 at row 0), column = N.V.
std::vector<float> BrdfLut(uint32_t size);

// The camera grade of Resource/INI/aces.json as the shader applies it (see ToGrade()).
struct Grade
{
    Vec3 scale{1.0f, 1.0f, 1.0f}; // colour filter x 2^exposure_ev
    float contrast = 1.0f;
    float saturation = 1.0f;
    Vec3 lift{0.0f, 0.0f, 0.0f}, gamma{1.0f, 1.0f, 1.0f}, gain{1.0f, 1.0f, 1.0f};
};
// False with `error` for a grade the shader does not implement (type other than "aces", temperature or tint != 0).
bool ToGrade(const scene_data::ToneMapping &tone, Grade &out, std::string &error);

// ---- lighting presets ---------------------------------------------------------------------------

// Constants of one hour, as the shaders use them. Colours are linear and already multiplied by their intensity.
struct HourLighting
{
    Vec3 sun_direction{0.0f, 1.0f, 0.0f}; // world, towards the sun
    bool sun_direction_known = false;      // the preset gives sun_azimuth and sun_height
    Vec3 sun{};
    Vec3 ambient{};
    float exposure = 1.0f;
    float emission = 0.0f; // the hour's emission strength (lit windows at night)
    std::string hdr_texture; // as written in the preset ("resource/lighting/Day.hdr")
    // Point-light groups (light type names of lights.ini, lower case): colour x intensity.
    std::vector<std::pair<std::string, Vec3>> point_groups;
};

// The hour `hour` (fractional, 0..24) of a preset: colours, exposure and emission blend linearly between the two
// neighbouring hours; sun direction and environment come from the earlier one. False with `error` when either hour
// is missing.
bool BlendedHourConstants(const scene_data::LightingPreset &preset, float hour, HourLighting &out, std::string &error);

// Sun direction from the preset's degrees: elevation above the horizon, azimuth from +Z towards +X.
Vec3 SunDirection(float azimuth_degrees, float height_degrees);
// The hour's constants. False with `error` when the preset has no entry for `hour` (0..23).
bool HourConstants(const scene_data::LightingPreset &preset, int hour, HourLighting &out, std::string &error);

// The preset of a location: every preset's locationId[] (case-insensitive). Several presets claiming the same id
// is a data error, reported by Add().
class LightingLibrary
{
  public:
    // Adds a parsed preset (file name for messages); returns errors for duplicate location ids.
    std::vector<std::string> Add(std::string file, scene_data::LightingPreset preset);
    const scene_data::LightingPreset *Find(std::string_view location_id) const;
    const std::string *FileOf(std::string_view location_id) const;
    size_t size() const { return presets_.size(); }

  private:
    struct Entry
    {
        std::string file;
        scene_data::LightingPreset preset;
    };
    std::vector<Entry> presets_;
    std::vector<std::pair<std::string, size_t>> ids_; // lower-case id -> preset index
};

} // namespace storm::pbr
