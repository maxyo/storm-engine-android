// Unit checks of the CPU side of the PBR package: material resolution, IBL bake (strip -> cube, GGX prefilter,
// SH9 irradiance), the BRDF table and the lighting preset constants. Synthetic data only.
#include "storm/pbr/environment.hpp"
#include "storm/pbr/material.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace
{
int failures = 0;

void Check(bool condition, const std::string &what)
{
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what.c_str());
    failures += !condition;
}

bool Near(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

using storm::pbr::Resolution;
using storm::pbr::Variant;
namespace sd = storm::scene_data;

sd::ModelMaterial Parse(const char *json)
{
    sd::Report report;
    sd::ModelSidecar sidecar = sd::ParseModelSidecar(json, report);
    if (!report.ok() || sidecar.models.size() != 1)
        std::printf("sidecar parse failed: %s\n", report.errors.empty() ? "no model" : report.errors.front().c_str());
    return sidecar.models.empty() ? sd::ModelMaterial{} : sidecar.models.begin()->second;
}

bool HasError(const storm::pbr::ResolveResult &r, const std::string &prefix)
{
    for (const std::string &e : r.errors)
        if (e.rfind(prefix, 0) == 0)
            return true;
    return false;
}

const storm::pbr::TextureRef *Unit(const storm::pbr::MaterialDesc &m, uint8_t unit)
{
    for (const auto &t : m.textures)
        if (t.unit == unit)
            return &t;
    return nullptr;
}

void MaterialChecks()
{
    using namespace storm::pbr;
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"rma_texture":"Resource\\Textures\\Location\\Stone_RMA.dds",
            "normal_texture":"Resource\\Textures\\Location\\Stone_nom.dds","alpha":0,"reflection":0,"path_preset":"NONE"}}})"));
        Check(r.resolution == Resolution::Pbr && r.material.variant == Variant::Standard, "rma + normal -> standard");
        const TextureRef *rma = Unit(r.material, kUnitRma), *nrm = Unit(r.material, kUnitNormal);
        Check(rma && rma->path == "Location\\Stone_RMA.dds" && nrm && nrm->path == "Location\\Stone_nom.dds",
              "texture paths relative to Resource\\Textures");
        Check(r.material.roughness < 0 && r.material.metallic < 0 && !r.material.alpha && !r.material.needs_white,
              "no overrides by default");
    }
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"rma_texture":"Resource\\Textures\\x_RMA.dds"}}})"));
        Check(r.resolution == Resolution::Pbr && r.material.variant == Variant::Flat, "rma only -> flat");
    }
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"rma_texture":"Resource\\Textures\\l_RMA.dds",
            "normal_texture":"Resource\\Textures\\l_nom.dds","emission_texture":"Resource\\Textures\\l_Emission.tga",
            "roughness":0.6,"metallic":0.0,"alpha":1}}})"));
        Check(r.resolution == Resolution::Pbr && r.material.variant == Variant::Emissive && r.material.emission == 1.0f &&
                  Unit(r.material, kUnitEmission) && r.material.alpha,
              "emission texture -> emissive, strength 1, alpha");
        Check(r.material.roughness == 0.6f && r.material.metallic == 0.0f, "roughness / metallic overrides kept");
    }
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"normal_texture":"Resource\\Textures\\n.dds","roughness":0.6,"metallic":0}}})"));
        Check(r.resolution == Resolution::Pbr && r.material.needs_white && !Unit(r.material, kUnitRma),
              "no rma but both overrides -> constant material on the white texture");
    }
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"normal_texture":"Resource\\Textures\\n.dds"}}})"));
        Check(r.resolution == Resolution::Rejected && HasError(r, "rma_texture"), "normal only, no overrides -> rejected");
    }
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"animation":"plant","alpha":1}}})"));
        Check(r.resolution == Resolution::NotPbr && r.errors.empty(), "flags only -> not a PBR material, no error");
    }
    {
        const auto r = ResolveMaterial(Parse(R"({"models":{"a":{"rma_texture":"x.dds","roughness":1.5}}})"));
        Check(r.resolution == Resolution::Rejected && HasError(r, "roughness"), "roughness out of range -> rejected");
    }
    const char *layer = R"({"models":{"t":{"layer_material":1,"mask_texture":"Resource\\Textures\\m.dds",
        "rma_texture":"Resource\\Textures\\b_RMA.dds","normal_texture":"Resource\\Textures\\b_nom.dds","base_tiling":1.5,
        "normal_strength":3,
        "r_albedo_texture":"Resource\\Textures\\r.dds","r_rma_texture":"Resource\\Textures\\r_rma.dds","r_normal_texture":"Resource\\Textures\\r_nom.dds","r_tiling":2,
        "g_albedo_texture":"Resource\\Textures\\g.dds","g_rma_texture":"Resource\\Textures\\g_rma.dds","g_normal_texture":"Resource\\Textures\\g_nom.dds","g_tiling":2,
        "b_albedo_texture":"Resource\\Textures\\b.dds","b_rma_texture":"Resource\\Textures\\b_rma.dds","b_normal_texture":"Resource\\Textures\\b_nom.dds","b_tiling":0.7}}})";
    {
        const auto r = ResolveMaterial(Parse(layer));
        Check(r.resolution == Resolution::Pbr && r.material.variant == Variant::Layer && r.material.textures.size() == 12,
              "layer material: base rma + normal + mask + 9 layer textures");
        Check(r.material.tiling[0] == 1.5f && r.material.tiling[1] == 2.0f && r.material.tiling[3] == 0.7f &&
                  r.material.normal_strength == 3.0f,
              "layer tilings and normal strength");
        bool units_ok = Unit(r.material, kUnitMask) && Unit(r.material, LayerAlbedoUnit(0)) && Unit(r.material, LayerAlbedoUnit(1)) &&
                        Unit(r.material, LayerAlbedoUnit(2)) && Unit(r.material, kUnitLayerRma + 2) &&
                        Unit(r.material, kUnitLayerNormal + 2);
        bool distinct = true;
        for (size_t i = 0; i < r.material.textures.size(); ++i)
            for (size_t j = i + 1; j < r.material.textures.size(); ++j)
                distinct = distinct && r.material.textures[i].unit != r.material.textures[j].unit;
        Check(units_ok && distinct, "layer units distinct, all below 16, none on the environment units");
        for (const auto &t : r.material.textures)
            units_ok = units_ok && t.unit < kUnitCount && t.unit != kUnitRadiance && t.unit != kUnitBrdf && t.unit != kUnitAlbedo &&
                       t.unit != kUnitAo;
        Check(units_ok, "layer units avoid 0, 1, 5, 6");
    }
    {
        std::string broken = layer;
        broken.replace(broken.find("\"g_rma_texture\""), std::string("\"g_rma_texture\"").size(), "\"g_unused_key\"");
        sd::Report report;
        const sd::ModelSidecar sidecar = sd::ParseModelSidecar(broken, report);
        const auto r = ResolveMaterial(sidecar.models.begin()->second);
        Check(r.resolution == Resolution::Rejected && HasError(r, "g_rma_texture: missing"), "layer without g_rma_texture -> rejected");
    }
    {
        std::string broken = layer;
        broken.replace(broken.find("\"b_tiling\":0.7"), std::string("\"b_tiling\":0.7").size(), "\"b_tiling\":0");
        sd::Report report;
        const auto r = ResolveMaterial(sd::ParseModelSidecar(broken, report).models.begin()->second);
        Check(r.resolution == Resolution::Rejected && HasError(r, "b_tiling"), "zero tiling -> rejected");
    }
}

// Linear RGBA32F horizontal strip, faces +X -X +Y -Y +Z -Z, every face one colour.
std::vector<float> Strip(uint32_t face, const float colors[6][3])
{
    std::vector<float> strip(size_t(face) * 6 * face * 4);
    for (uint32_t y = 0; y < face; ++y)
        for (uint32_t x = 0; x < face * 6; ++x)
        {
            float *p = &strip[(size_t(y) * face * 6 + x) * 4];
            const float *c = colors[x / face];
            p[0] = c[0], p[1] = c[1], p[2] = c[2], p[3] = 1.0f;
        }
    return strip;
}

void EnvironmentChecks()
{
    using namespace storm::pbr;
    {
        const float grey[6][3] = {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
                                  {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}};
        const std::vector<float> strip = Strip(64, grey);
        Environment env;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        const bool ok = BakeEnvironmentFromStrip(strip.data(), 64 * 6, 64, {.face_size = 32}, env, error);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Check(ok, "bake a constant 64-texel strip to a 32-texel cube " + error + " (" + std::to_string(ms) + " ms)");
        Check(env.radiance.size == 32 && env.radiance.mips == 6, "radiance cube 32 with 6 mips");
        float worst = 0.0f;
        for (uint32_t mip = 0; mip < env.radiance.mips; ++mip)
            for (uint32_t face = 0; face < 6; ++face)
            {
                const uint32_t n = env.radiance.MipSize(mip);
                const float *t = &env.radiance.rgba[env.radiance.MipOffset(mip, face)];
                for (uint32_t i = 0; i < n * n; ++i)
                    for (int c = 0; c < 3; ++c)
                        worst = std::max(worst, std::fabs(t[i * 4 + c] - 0.5f));
            }
        Check(worst < 0.01f, "GGX prefilter of a constant environment is constant on every mip (worst " + std::to_string(worst) + ")");
        const Vec3 e = EvaluateIrradiance(env.irradiance, {0.3f, 0.8f, -0.2f});
        Check(Near(e[0], 0.5f, 0.005f) && Near(e[2], 0.5f, 0.005f),
              "SH9 irradiance/pi of a constant 0.5 environment is 0.5 (" + std::to_string(e[0]) + ")");
    }
    {
        // Faces in strip order with distinct colours: the cube face order must be +X -X +Y -Y +Z -Z.
        const float colors[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}};
        const std::vector<float> strip = Strip(32, colors);
        Environment env;
        std::string error;
        Check(BakeEnvironmentFromStrip(strip.data(), 32 * 6, 32, {.face_size = 32}, env, error), "bake a six-colour strip " + error);
        bool order = true;
        for (uint32_t face = 0; face < 6; ++face)
        {
            const float *t = &env.radiance.rgba[env.radiance.MipOffset(0, face) + (16 * 32 + 16) * 4];
            order = order && Near(t[0], colors[face][0], 1e-4f) && Near(t[1], colors[face][1], 1e-4f) && Near(t[2], colors[face][2], 1e-4f);
        }
        Check(order, "strip faces land on cube faces +X -X +Y -Y +Z -Z");
        // A lit +Y face only: irradiance facing up is larger than facing down, sideways in between.
        const float up_only[6][3] = {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        const std::vector<float> sky = Strip(32, up_only);
        Check(BakeEnvironmentFromStrip(sky.data(), 32 * 6, 32, {.face_size = 32}, env, error), "bake a top-lit strip");
        const float up = EvaluateIrradiance(env.irradiance, {0, 1, 0})[0], down = EvaluateIrradiance(env.irradiance, {0, -1, 0})[0],
                    side = EvaluateIrradiance(env.irradiance, {1, 0, 0})[0];
        // Analytic: the form factor of the +Y face seen from the centre facing up is 4 x 0.1385 = 0.554 (point to
        // parallel square); SH9 is band-limited, hence the tolerance.
        Check(up > side && side > down && Near(down, 0.0f, 0.04f) && Near(up, 0.554f, 0.06f),
              "SH9 of a top-lit environment: up " + std::to_string(up) + " > side " + std::to_string(side) + " > down " + std::to_string(down));
        Check(CubeTexelDirection(2, 16, 16, 32)[1] > 0.99f && CubeTexelDirection(4, 16, 16, 32)[2] > 0.99f &&
                  CubeTexelDirection(1, 16, 16, 32)[0] < -0.99f,
              "cube texel directions follow the D3D / bgfx face convention");
    }
    {
        std::vector<float> bad(10 * 10 * 4, 1.0f);
        Environment env;
        std::string error;
        const bool square = BakeEnvironmentFromStrip(bad.data(), 10, 10, {}, env, error);
        Check(!square && error.find("not a cube strip") != std::string::npos, "a square image is refused: " + error);
        error.clear();
        const std::vector<uint8_t> junk(64, 7);
        const bool decoded = BakeEnvironment(junk, {}, env, error);
        Check(!decoded && error.find("cannot decode") != std::string::npos, "junk bytes are refused: " + error);
    }
    {
        // Cost of the default bake (face 64) from a 256-texel strip, the smallest cmft strip in the game.
        const float grey[6][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
        const std::vector<float> strip = Strip(256, grey);
        Environment env;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        const bool ok = BakeEnvironmentFromStrip(strip.data(), 256 * 6, 256, {}, env, error);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Check(ok && env.radiance.size == BakeOptions{}.face_size, "default bake from a 256 strip: " + std::to_string(ms) + " ms");
    }
    {
        const uint32_t n = 32;
        const std::vector<float> lut = BrdfLut(n);
        // Smooth surface seen head-on: scale ~1, bias ~0. Rough grazing: both small.
        const float s0 = lut[(0 * n + (n - 1)) * 2], b0 = lut[(0 * n + (n - 1)) * 2 + 1];
        Check(s0 > 0.9f && b0 < 0.05f, "BRDF LUT at roughness 0, N.V 1: (" + std::to_string(s0) + ", " + std::to_string(b0) + ")");
        bool bounded = true;
        for (size_t i = 0; i < lut.size(); i += 2)
            bounded = bounded && lut[i] >= 0.0f && lut[i + 1] >= 0.0f && lut[i] + lut[i + 1] <= 1.02f;
        Check(bounded, "BRDF LUT: scale, bias >= 0 and scale + bias <= 1 (energy)");
        const float rough = lut[((n - 1) * n + n / 2) * 2] + lut[((n - 1) * n + n / 2) * 2 + 1];
        Check(rough < s0 + b0, "rough surfaces reflect less of F0 + (1-F0) than smooth ones");
    }
}

void PresetChecks()
{
    using namespace storm::pbr;
    const Vec3 zenith = SunDirection(0.0f, 90.0f), east = SunDirection(90.0f, 0.0f), north = SunDirection(0.0f, 0.0f);
    Check(Near(zenith[1], 1.0f, 1e-5f) && Near(east[0], 1.0f, 1e-5f) && Near(north[2], 1.0f, 1e-5f),
          "sun direction: height 90 = up, azimuth 90 = +X, azimuth 0 = +Z");
    sd::Report report;
    const sd::LightingPreset preset = sd::ParseLightingPreset(R"({"schemaVersion":1,"presetName":"T","locationId":["Town_A","Town_B"],
        "sun_azimuth":90,"sun_height":30,
        "hours":{"12":{"ambient":{"color":[1,0.5,0.25],"intensity":2},"sun":{"color":[1,1,0.5],"intensity":3},
        "exposure":2,"emission":0.5,"hdr_texture":"resource/lighting/Day.hdr"}}})",
                                                              report);
    Check(report.ok(), "preset parses");
    HourLighting hour;
    std::string error;
    Check(HourConstants(preset, 12, hour, error), "hour 12 present");
    Check(hour.ambient[0] == 2.0f && hour.ambient[2] == 0.5f && hour.sun[2] == 1.5f && hour.exposure == 2.0f && hour.emission == 0.5f &&
              hour.sun_direction_known && Near(hour.sun_direction[1], 0.5f, 1e-5f) && hour.hdr_texture == "resource/lighting/Day.hdr",
          "hour constants: colour x intensity, exposure, emission, sun direction, hdr");
    const bool missing = !HourConstants(preset, 3, hour, error);
    Check(missing && error.find("no hour 3") != std::string::npos, "missing hour refused: " + error);
    {
        sd::Report r2;
        const sd::LightingPreset two = sd::ParseLightingPreset(R"({"hours":{
            "5":{"ambient":{"color":[1,1,1],"intensity":1},"sun":{"color":[1,1,1],"intensity":0},"exposure":1,"hdr_texture":"a.hdr",
                 "point":{"Candle":{"color":[1,0.5,0],"intensity":2}}},
            "6":{"ambient":{"color":[1,1,1],"intensity":3},"sun":{"color":[1,1,1],"intensity":2},"exposure":2,"hdr_texture":"b.hdr",
                 "point":{"Candle":{"color":[1,0.5,0],"intensity":4}}}}})",
                                                               r2);
        HourLighting blended;
        Check(BlendedHourConstants(two, 5.25f, blended, error) && Near(blended.ambient[0], 1.5f, 1e-5f) && Near(blended.sun[0], 0.5f, 1e-5f) &&
                  Near(blended.exposure, 1.25f, 1e-5f) && blended.hdr_texture == "a.hdr" && blended.point_groups.size() == 1 &&
                  blended.point_groups[0].first == "candle" && Near(blended.point_groups[0].second[0], 2.5f, 1e-5f),
              "hour 5.25 blends colours, exposure and point groups a quarter towards hour 6; environment of hour 5");
        const bool refused = !BlendedHourConstants(two, 6.5f, blended, error);
        Check(refused, "blending into a missing hour is refused: " + error);
    }
    {
        sd::Report r3;
        const sd::ToneMapping tone = sd::ParseToneMapping(R"({"type":"aces","version":1,"exposure_ev":1,"contrast":0.7,
            "saturation":1.2,"color_filter":[1,0.5,1],"lift":[0,0,0],"gamma":[1,1,1],"gain":[1,1,1],"temperature":0,"tint":0})",
                                                          r3);
        Grade grade;
        Check(r3.ok() && r3.unknown_keys.empty() && ToGrade(tone, grade, error) && Near(grade.scale[0], 2.0f, 1e-6f) &&
                  Near(grade.scale[1], 1.0f, 1e-6f) && grade.contrast == 0.7f && grade.saturation == 1.2f,
              "aces.json -> grade (2^ev x filter, contrast, saturation)");
        sd::ToneMapping other = tone;
        other.type = "reinhard";
        bool refused = !ToGrade(other, grade, error);
        Check(refused, "unknown tone mapping type refused: " + error);
        other = tone;
        other.temperature = 0.1f;
        refused = !ToGrade(other, grade, error);
        Check(refused, "white balance refused (not implemented): " + error);
    }
    LightingLibrary library;
    Check(library.Add("T.json", preset).empty(), "library add");
    sd::LightingPreset other = preset;
    other.preset_name = "U";
    const auto errors = library.Add("U.json", other);
    Check(errors.size() == 2, "duplicate location ids reported (" + std::to_string(errors.size()) + ")");
    Check(library.Find("town_a") && library.Find("town_a")->preset_name == "T" && *library.FileOf("TOWN_B") == "T.json" &&
              !library.Find("Town_C"),
          "location lookup is case-insensitive and keeps the first claim");
}
} // namespace

int main()
{
    MaterialChecks();
    EnvironmentChecks();
    PresetChecks();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
