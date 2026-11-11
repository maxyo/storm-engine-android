// Unit checks for the CPU side of the fx package: grading LUTs, tone curve,
// baked noise, the engine wave model and its GPU/Gerstner mappings, the sea
// grid, the sky model, lighting and point-light selection, the shadow cascade and the
// explicit handling of unsupported settings. No GPU is needed.
#include "storm/fx/capabilities.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/light_set.hpp"
#include "storm/fx/lighting_model.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/noise_bake.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/shadow_model.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/fx/water_grid.hpp"
#include "storm/fx/wave_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace storm::fx;

namespace
{
int failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                                        \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

bool Near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

float Chroma(std::array<float, 3> c)
{
    return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
}

void CheckSettings()
{
    const Settings original = Settings::FromPreset(QualityPreset::Low);
    CHECK(original.PostIsIdentity());
    CHECK(original.water == WaterQuality::Legacy);
    CHECK(!original.sky_atmosphere);
    CHECK(original.lighting_flags == kLightingNone);
    const Settings medium = Settings::FromPreset(QualityPreset::Medium);
    const Settings high = Settings::FromPreset(QualityPreset::High);
    CHECK(!medium.PostIsIdentity() && !high.PostIsIdentity());
    CHECK(medium.bloom_downscale == 4 && high.bloom_downscale == 2);
    CHECK(!medium.sharpen && !high.sharpen);
    // MSAA is the primary anti-aliasing of the presets; FXAA is left to the player. Optional looks stay off.
    CHECK(medium.msaa == 4 && high.msaa == 4 && !medium.fxaa && !high.fxaa && original.msaa == 1);
    CHECK(medium.anisotropy == 4 && high.anisotropy == 8 && original.anisotropy == 1);
    CHECK(high.vignette == 0.0f && high.film_grain == 0.0f && medium.dither && !original.dither);
    CHECK(high.water == WaterQuality::High && medium.water == WaterQuality::Medium);
    CHECK((medium.lighting_flags & kLightingPerPixel) && !(medium.lighting_flags & kLightingSpecular));
    CHECK(medium.shadows == ShadowQuality::Low && high.shadows == ShadowQuality::Soft && original.shadows == ShadowQuality::Off);
    CHECK(!medium.ssao && high.ssao && !original.ssao);
    // The presets keep the legacy look: no tone curve, grade, flare, shafts, fx sky, hemisphere or linear sums.
    for (const Settings *preset : {&medium, &high})
        CHECK(!preset->tonemap && !preset->scene_grading && !preset->lens_flare && !preset->sun_shafts &&
              !preset->sky_atmosphere && preset->grade == GradePreset::Caribbean && preset->grade_strength <= 0.4f &&
              !(preset->lighting_flags & (kLightingHemisphere | kLightingLinear | kLightingRim | kLightingAerial)));
    Settings graded = original;
    graded.grade = GradePreset::Caribbean;
    CHECK(!graded.PostIsIdentity());
    graded.grade_strength = 0.0f;
    CHECK(graded.PostIsIdentity());
    CHECK(NameRu(QualityPreset::High) == "Высокая" && NameRu(GradePreset::Overcast) == "Пасмурно");
}

void CheckLut()
{
    // Identity: every texel is its own coordinate, and the trilinear fetch returns the input.
    const int n = kDefaultLutSize;
    const std::vector<uint8_t> identity = BuildLutRgba8(ColorGrade::Preset(GradePreset::Original), n);
    CHECK(identity.size() == static_cast<size_t>(n) * n * n * 4);
    bool exact = true;
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r)
            {
                const uint8_t *t = &identity[((static_cast<size_t>(b) * n + g) * n + r) * 4];
                const auto expect = [](int i) { return static_cast<int>(std::lround(i * 255.0f / (n - 1))); };
                exact = exact && t[0] == expect(r) && t[1] == expect(g) && t[2] == expect(b) && t[3] == 255;
            }
    CHECK(exact);
    for (float v : {0.0f, 0.1f, 0.37f, 0.5f, 0.93f, 1.0f})
    {
        const auto out = SampleLut(identity, n, {v, 1.0f - v, 0.5f * v});
        CHECK(Near(out[0], v, 0.003f) && Near(out[1], 1.0f - v, 0.003f) && Near(out[2], 0.5f * v, 0.003f));
    }

    const ColorGrade caribbean = ColorGrade::Preset(GradePreset::Caribbean);
    const ColorGrade overcast = ColorGrade::Preset(GradePreset::Overcast);
    CHECK(ColorGrade{}.IsIdentity() && !caribbean.IsIdentity() && !overcast.IsIdentity());

    // Black and white stay (nearly) black and white, greys stay monotonic.
    for (const ColorGrade &grade : {caribbean, overcast})
    {
        CHECK(grade.Apply({0.0f, 0.0f, 0.0f})[1] < 0.03f);
        CHECK(grade.Apply({1.0f, 1.0f, 1.0f})[1] > 0.93f);
        float previous = -1.0f;
        for (int i = 0; i <= 64; ++i)
        {
            const float v = static_cast<float>(i) / 64.0f;
            const auto out = grade.Apply({v, v, v});
            const float luma = 0.2126f * out[0] + 0.7152f * out[1] + 0.0722f * out[2];
            CHECK(luma >= previous - 1e-4f);
            previous = luma;
            for (float c : out)
                CHECK(c >= 0.0f && c <= 1.0f);
        }
    }
    // «Карибы» is warmer and more saturated, «Пасмурно» cooler and less saturated.
    const std::array<float, 3> sea{0.20f, 0.45f, 0.60f}, grey{0.5f, 0.5f, 0.5f};
    CHECK(Chroma(caribbean.Apply(sea)) > Chroma(sea) * 1.08f);
    CHECK(Chroma(overcast.Apply(sea)) < Chroma(sea) * 0.85f);
    CHECK(caribbean.Apply(grey)[0] > caribbean.Apply(grey)[2] + 0.02f);
    CHECK(overcast.Apply(grey)[2] > overcast.Apply(grey)[0] + 0.02f);
    // Neither preset changes mid-grey brightness by much.
    CHECK(Near(caribbean.Apply(grey)[1], 0.5f, 0.05f) && Near(overcast.Apply(grey)[1], 0.5f, 0.06f));
    // Interpolation: the end points are the presets, the middle is in between.
    const ColorGrade half = ColorGrade::Lerp(ColorGrade{}, caribbean, 0.5f);
    CHECK(Near(half.saturation, 0.5f * (1.0f + caribbean.saturation), 1e-6f));
    CHECK(ColorGrade::Lerp(ColorGrade{}, caribbean, 0.0f).IsIdentity());
    // A LUT of another size has the right number of texels.
    CHECK(BuildLutRgba8(caribbean, 16).size() == 16u * 16u * 16u * 4u);
}

void CheckToneCurve()
{
    // Mid grey keeps its brightness, the curve is monotonic and stays inside 0..1.
    const float grey = std::sqrt(0.18f);
    CHECK(Near(TonemapTransfer(grey), grey, 0.004f));
    float previous = -1.0f;
    for (int i = 0; i <= 100; ++i)
    {
        const float v = TonemapTransfer(static_cast<float>(i) / 100.0f);
        CHECK(v >= previous && v >= 0.0f && v <= 1.0f);
        previous = v;
    }
    // Filmic shape: darker shadows, white not clipped.
    CHECK(TonemapTransfer(0.2f) < 0.2f);
    CHECK(TonemapTransfer(1.0f) > 0.9f && TonemapTransfer(1.0f) < 1.0f);
    // At the default strength shadows are only mildly deepened.
    const Settings defaults;
    CHECK(TonemapTransfer(0.1f, 1.0f, defaults.tonemap_strength) > 0.055f);
    CHECK(Near(TonemapTransfer(0.37f, 1.0f, 0.0f), 0.37f, 1e-5f));
    CHECK(Near(ExpandLdr(1.0f), 3.0f, 1e-4f) && Near(ExpandLdr(0.0f), 0.0f, 1e-6f));
}

void CheckNoise()
{
    const BakedTexture water = BakeWaterDetail(128, 1u);
    CHECK(water.size == 128 && water.mip_count == 8);
    size_t expected = 0;
    for (int s = 128; s >= 1; s /= 2)
        expected += static_cast<size_t>(s) * s * 4;
    CHECK(water.rgba.size() == expected);
    // Deterministic for a seed, different for another.
    CHECK(BakeWaterDetail(128, 1u).rgba == water.rgba);
    CHECK(BakeWaterDetail(128, 2u).rgba != water.rgba);
    // Tiling: the step across the wrap is no larger than the largest step inside the texture.
    int inside = 0, wrap = 0;
    double slope_sum = 0.0;
    for (int y = 0; y < 128; ++y)
    {
        for (int x = 0; x + 1 < 128; ++x)
            inside = std::max(inside, std::abs(water.rgba[(y * 128 + x) * 4 + 3] - water.rgba[(y * 128 + x + 1) * 4 + 3]));
        wrap = std::max(wrap, std::abs(water.rgba[(y * 128 + 127) * 4 + 3] - water.rgba[(y * 128) * 4 + 3]));
        wrap = std::max(wrap, std::abs(water.rgba[(127 * 128 + y) * 4 + 3] - water.rgba[y * 4 + 3]));
        for (int x = 0; x < 128; ++x)
            slope_sum += water.rgba[(y * 128 + x) * 4 + 0] - 127.5;
    }
    CHECK(wrap <= inside);
    // Slopes average to zero (a flat sea on average) and use a useful part of the range.
    CHECK(std::fabs(slope_sum / (128.0 * 128.0)) < 3.0);
    const auto minmax = std::minmax_element(water.rgba.begin(), water.rgba.begin() + 128 * 128 * 4);
    CHECK(*minmax.second - *minmax.first > 150);
    // The 1x1 mip of the slopes is the neutral value.
    CHECK(std::abs(water.rgba[water.rgba.size() - 4] - 128) <= 3);

    const BakedTexture clouds = BakeCloudNoise(64, 3u);
    CHECK(clouds.size == 64 && clouds.mip_count == 7);
    for (int i = 0; i < 300; ++i)
    {
        const float u = 0.013f * i, v = 0.007f * i;
        CHECK(Near(PeriodicFbm(u, v, 4, 3, 9u), PeriodicFbm(u + 1.0f, v + 2.0f, 4, 3, 9u), 2e-3f));
        CHECK(std::fabs(PeriodicFbm(u, v, 4, 3, 9u)) <= 1.0f);
    }
}

// A synthetic stand-in for the engine's animated height frames (no game data).
void FillLayer(EngineWaveField &field, int layer, float phase)
{
    const int n = EngineWaveField::kSize;
    std::vector<float> heights(EngineWaveField::kTexels), normals(2 * EngineWaveField::kTexels);
    const auto h = [&](int x, int z) {
        const float u = 6.2831853f * static_cast<float>(x & (n - 1)) / n, v = 6.2831853f * static_cast<float>(z & (n - 1)) / n;
        return 0.5f + 0.25f * std::sin(3.0f * u + phase) * std::cos(2.0f * v) + 0.2f * std::sin(5.0f * v - phase + u);
    };
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x)
        {
            heights[z * n + x] = h(x, z);
            // Unit normal of the height field in texel units, as sea.cpp stores it (x and z components).
            const float dx = 0.5f * (h(x + 1, z) - h(x - 1, z)), dz = 0.5f * (h(x, z + 1) - h(x, z - 1));
            const float len = std::sqrt(dx * dx + 1.0f + dz * dz);
            normals[2 * (z * n + x) + 0] = -dx / len;
            normals[2 * (z * n + x) + 1] = -dz / len;
        }
    field.SetLayer(layer, heights, normals);
}

void CheckEngineWaves()
{
    EngineWaveField field;
    FillLayer(field, 0, 0.3f);
    FillLayer(field, 1, 1.9f);
    EngineSeaState sea;
    sea.layers[0] = {6.0f, 0.5f, {3.0f, -2.0f}};
    sea.layers[1] = {0.75f, 2.0f, {-1.0f, 4.5f}};
    sea.pos_shift = 1.2f;
    sea.bump_scale = 0.05f;

    // At texel centres of layer 1 with layer 2 switched off, the height is amp * texel value.
    EngineSeaState single = sea;
    single.layers[1].amp = 0.0f;
    single.layers[0].move = {0.0f, 0.0f};
    float normal[3];
    const float at_texel = field.Height(single, 10.0f / 0.5f, 7.0f / 0.5f, normal);
    const float u = 6.2831853f * 10.0f / 128.0f, v = 6.2831853f * 7.0f / 128.0f;
    CHECK(Near(at_texel, 6.0f * (0.5f + 0.25f * std::sin(3.0f * u + 0.3f) * std::cos(2.0f * v) + 0.2f * std::sin(5.0f * v - 0.3f + u)), 1e-4f));
    CHECK(Near(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2], 1.0f, 1e-5f) && normal[1] > 0.0f);

    // Bilinear: the midpoint between two texels is their average.
    const float a = field.Height(single, 20.0f, 14.0f), b = field.Height(single, 22.0f, 14.0f);
    CHECK(Near(field.Height(single, 21.0f, 14.0f), 0.5f * (a + b), 1e-4f));
    // The field tiles every 128 / scale metres and scrolls with `move`.
    CHECK(Near(field.Height(sea, 5.3f, -8.1f), field.Height(sea, 5.3f + 256.0f, -8.1f + 256.0f), 1e-3f));
    EngineSeaState moved = single;
    moved.layers[0].move = {4.0f, 0.0f};
    CHECK(Near(field.Height(moved, 16.0f, 14.0f), field.Height(single, 20.0f, 14.0f), 1e-4f));
    // Heights stay inside 0 .. amp1 + amp2, the normal leans against the slope.
    for (int i = 0; i < 500; ++i)
    {
        const float x = 0.37f * i - 40.0f, z = 0.91f * i - 100.0f;
        const float y = field.Height(sea, x, z, normal);
        CHECK(y >= 0.0f && y <= 6.75f);
        const float slope = (field.Height(sea, x + 0.05f, z) - field.Height(sea, x - 0.05f, z)) / 0.1f;
        if (std::fabs(slope) > 0.15f)
            CHECK(normal[0] * slope < 0.0f);
    }
    // Vertex construction as in SEA::SSE_WaveXZBlock.
    const SeaVertex vertex = field.Displace(sea, 12.0f, -3.0f, {10.0f, -5.0f});
    float n[3];
    const float y = field.Height(sea, 12.0f, -3.0f, n);
    CHECK(Near(vertex.pos[1], y, 1e-6f));
    CHECK(Near(vertex.pos[0], 12.0f - n[0] * 3.6f - 10.0f, 1e-5f) && Near(vertex.pos[2], -3.0f - n[2] * 3.6f + 5.0f, 1e-5f));
    CHECK(Near(vertex.uv[0], (12.0f - n[0] * 5.3f) * 0.05f, 1e-6f) && Near(vertex.uv[1], (-3.0f - n[2] * 5.3f) * 0.05f, 1e-6f));

    // GPU texture: half floats round-trip, mip 0 holds the texels, the last mip the mean.
    for (float value : {0.0f, 1.0f, -1.0f, 0.5f, 0.333333f, -0.72f, 1e-3f, 65504.0f, 3.0e-6f})
        CHECK(Near(HalfToFloat(FloatToHalf(value)), value, std::max(std::fabs(value) * 6e-4f, 4e-8f)));
    const std::vector<uint16_t> packed = field.PackLayerHalf(0);
    size_t expected = 0;
    for (int s = 128; s >= 1; s /= 2)
        expected += static_cast<size_t>(s) * s * 4;
    CHECK(packed.size() == expected);
    CHECK(Near(HalfToFloat(packed[4 * (7 * 128 + 10)]), field.Height(single, 20.0f, 14.0f) / 6.0f, 1e-3f));
    CHECK(Near(HalfToFloat(packed[packed.size() - 4]), 0.5f, 0.01f));
}

void CheckGerstner()
{
    GerstnerSource source;
    source.amp = {6.0f, 0.75f};
    source.scale = {0.5f, 2.0f};
    source.move_speed = {{{5.0f, 0.0f}, {0.0f, 2.0f}}};
    source.pos_shift = 1.2f;
    const GerstnerSet set = GerstnerFromEngine(source);
    CHECK(Near(set.mean_level, 3.375f, 1e-6f));

    float amplitude = 0.0f, steepness = 0.0f;
    for (const GerstnerWave &w : set.waves)
    {
        amplitude += w.amplitude;
        steepness += w.wave_number * w.horizontal;
        CHECK(Near(std::hypot(w.dir[0], w.dir[1]), 1.0f, 1e-5f));
        CHECK(w.wave_number > 0.0f && w.omega > 0.0f);
    }
    // Same height range as the engine layers (0 .. amp1 + amp2) and no self-intersection.
    CHECK(Near(amplitude, 0.5f * 6.75f, 1e-4f));
    CHECK(steepness <= 0.8f + 1e-4f);
    // The primary wave of layer 1 is half a tile long and travels against the scroll vector.
    CHECK(Near(6.2831853f / set.waves[0].wave_number, 128.0f, 1e-2f));
    CHECK(set.waves[0].dir[0] < -0.99f);
    CHECK(set.waves[3].dir[1] < -0.99f);

    float min_y = 1e9f, max_y = -1e9f;
    for (int i = 0; i < 2000; ++i)
    {
        const float x = 1.7f * i, z = -0.9f * i, t = 0.01f * i;
        float p[3], n[3];
        EvaluateGerstner(set, x, z, t, p, n);
        min_y = std::min(min_y, p[1]);
        max_y = std::max(max_y, p[1]);
        CHECK(Near(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0f, 1e-4f) && n[1] > 0.0f);
        if (i % 50 == 0)
        {
            // The analytic normal is perpendicular to the surface tangents.
            float px[3], pz[3];
            EvaluateGerstner(set, x + 0.01f, z, t, px, nullptr);
            EvaluateGerstner(set, x, z + 0.01f, t, pz, nullptr);
            const float tx = n[0] * (px[0] - p[0]) + n[1] * (px[1] - p[1]) + n[2] * (px[2] - p[2]);
            const float tz = n[0] * (pz[0] - p[0]) + n[1] * (pz[1] - p[1]) + n[2] * (pz[2] - p[2]);
            CHECK(std::fabs(tx) < 2e-4f && std::fabs(tz) < 2e-4f);
        }
    }
    CHECK(min_y >= -1e-3f && max_y <= 6.75f + 1e-3f && max_y - min_y > 2.5f);

    // A calm sea is flat, and stronger PosShift never produces loops.
    GerstnerSource calm = source;
    calm.amp = {0.0f, 0.0f};
    float p[3], n[3];
    EvaluateGerstner(GerstnerFromEngine(calm), 3.0f, 4.0f, 5.0f, p, n);
    CHECK(Near(p[1], 0.0f, 1e-6f) && Near(n[1], 1.0f, 1e-6f));
    GerstnerSource sharp = source;
    sharp.pos_shift = 20.0f;
    float sum = 0.0f;
    for (const GerstnerWave &w : GerstnerFromEngine(sharp).waves)
        sum += w.wave_number * w.horizontal;
    CHECK(sum <= 0.8f + 1e-4f);
}

void CheckGrid()
{
    for (int cells : {48, 64, 96})
    {
        const WaterGridMesh mesh = BuildWaterGrid(cells);
        CHECK(mesh.cells == cells);
        CHECK(mesh.vertices.size() == static_cast<size_t>(cells + 1) * (cells + 1) * 3);
        CHECK(mesh.full.size() == static_cast<size_t>(cells) * cells * 6);
        for (const auto &ring : mesh.rings)
            CHECK(ring.size() == static_cast<size_t>(cells * cells - (cells / 2) * (cells / 2)) * 6);
        // Edge codes: corners and even edge vertices 1, odd edge vertices 2 (edge along x) or 3 (along z).
        const int side = cells + 1;
        const auto code = [&](int x, int z) { return mesh.vertices[(static_cast<size_t>(z) * side + x) * 3 + 2]; };
        CHECK(code(0, 0) == 1.0f && code(cells, cells) == 1.0f && code(2, 0) == 1.0f);
        CHECK(code(1, 0) == 2.0f && code(3, cells) == 2.0f && code(0, 1) == 3.0f && code(cells, 5) == 3.0f);
        CHECK(code(1, 1) == 0.0f && code(cells / 2, cells / 2) == 0.0f);
    }
    CHECK(BuildWaterGrid(50).cells == 48);

    // Placement: for many camera positions the levels tile the plane exactly once.
    const int cells = 16;
    const WaterGridMesh mesh = BuildWaterGrid(cells);
    for (int sample = 0; sample < 40; ++sample)
    {
        const float cx = 37.3f * sample - 500.0f, cz = -11.7f * sample + 123.0f;
        const std::vector<WaterGridLevel> levels = PlaceWaterGrid(cx, cz, 0.5f, 4);
        CHECK(levels.size() == 4 && levels[0].ring == -1);
        // Count coverage on the finest lattice (quarter-cell sample points avoid the cell borders).
        std::map<std::pair<long, long>, int> coverage;
        for (const WaterGridLevel &level : levels)
        {
            CHECK(std::fabs(cx - level.origin[0]) <= 2.0f * level.step && std::fabs(cz - level.origin[1]) <= 2.0f * level.step);
            const std::vector<uint16_t> &indices = level.ring < 0 ? mesh.full : mesh.rings[level.ring];
            for (size_t i = 0; i < indices.size(); i += 6)
            {
                // Each cell is two triangles; its first vertex is the cell's lower corner.
                const float x0 = level.origin[0] + mesh.vertices[indices[i] * 3 + 0] * level.step;
                const float z0 = level.origin[1] + mesh.vertices[indices[i] * 3 + 1] * level.step;
                const int fine = static_cast<int>(std::lround(level.step / 0.5f));
                for (int dz = 0; dz < fine; ++dz)
                    for (int dx = 0; dx < fine; ++dx)
                        ++coverage[{std::lround(x0 / 0.5f) + dx, std::lround(z0 / 0.5f) + dz}];
            }
        }
        bool once = true;
        for (const auto &entry : coverage)
            once = once && entry.second == 1;
        CHECK(once);
        // The union is the outermost level's square.
        CHECK(coverage.size() == static_cast<size_t>(cells * 8) * (cells * 8));
    }
}

void CheckSky()
{
    const auto sky_for = [](float sun_height, float overcast) {
        WeatherInput weather;
        const float c = std::sqrt(1.0f - sun_height * sun_height);
        weather.sun_dir = {c, sun_height, 0.0f};
        weather.sun_color = {0.86f, 0.82f, 0.78f};
        weather.ambient_color = {0.37f, 0.37f, 0.35f};
        weather.fog_color = {0.31f, 0.47f, 0.61f};
        weather.overcast = overcast;
        return ComputeSky(weather);
    };
    const SkyState noon = sky_for(0.85f, 0.0f);
    CHECK(noon.lut.size() == static_cast<size_t>(kSkyLutWidth) * kSkyLutHeight * 4);
    const Vec3 zenith = SkyColor(noon, {0.0f, 1.0f, 0.0f});
    const Vec3 horizon = SkyColor(noon, {-1.0f, 0.0f, 0.0f});
    // Clear day: blue zenith, brighter and paler horizon.
    CHECK(zenith[2] > zenith[0] * 1.8f && zenith[2] > zenith[1]);
    CHECK(horizon[0] + horizon[1] + horizon[2] > zenith[0] + zenith[1] + zenith[2]);
    CHECK(horizon[2] / std::max(horizon[0], 1e-4f) < zenith[2] / std::max(zenith[0], 1e-4f));
    CHECK(noon.night < 0.01f);

    // Sunset: the horizon towards the sun is warm, the opposite side is not; sunlight reddens.
    const SkyState sunset = sky_for(0.03f, 0.0f);
    const Vec3 towards = SkyColor(sunset, {0.999f, 0.04f, 0.0f});
    const Vec3 away = SkyColor(sunset, {-0.999f, 0.04f, 0.0f});
    CHECK(towards[0] > towards[2] * 1.15f);
    CHECK(towards[0] / std::max(towards[2], 1e-4f) > 1.3f * away[0] / std::max(away[2], 1e-4f));
    CHECK(sunset.sun_light[0] > sunset.sun_light[2] * 1.2f);
    CHECK(noon.sun_light[0] < noon.sun_light[2] * 1.5f);

    // Night: dark but not black, no direct light.
    const SkyState night = sky_for(-0.5f, 0.0f);
    const Vec3 night_zenith = SkyColor(night, {0.0f, 1.0f, 0.0f});
    CHECK(night.night > 0.99f);
    CHECK(night_zenith[2] > 0.002f && night_zenith[2] < 0.06f);
    CHECK(night.sun_light[0] < 1e-3f);

    // Overcast: grey, darker dome, more cloud, weaker sun.
    const SkyState storm = sky_for(0.85f, 1.0f);
    const Vec3 storm_zenith = SkyColor(storm, {0.0f, 1.0f, 0.0f});
    CHECK(storm_zenith[2] / std::max(storm_zenith[0], 1e-4f) < 1.6f);
    CHECK(storm.cloud_cover > noon.cloud_cover + 0.3f);
    CHECK(storm.sun_light[1] < noon.sun_light[1] * 0.5f);

    // Fog colour is the horizon in the viewing direction, and its average is `horizon`.
    const Vec3 fog = FogColor(noon, {-1.0f, 0.0f});
    CHECK(Near(fog[0], horizon[0], 1e-4f) && Near(fog[2], horizon[2], 1e-4f));
    CHECK(noon.horizon[1] > 0.5f * fog[1] && noon.horizon[1] < 2.0f * fog[1]);
    // Below the horizon the panorama clamps to the horizon row.
    const Vec3 below = SkyColor(noon, {-0.8f, -0.6f, 0.0f});
    CHECK(Near(below[1], horizon[1], 1e-4f));

    WeatherInput a, b;
    CHECK(!SkyNeedsRebuild(a, b));
    b.wind = {0.0f, 1.0f};
    CHECK(!SkyNeedsRebuild(a, b));
    b.overcast = 0.5f;
    CHECK(SkyNeedsRebuild(a, b));
}

void CheckLighting()
{
    LightingInput input;
    input.sun_dir = {0.6f, 0.8f, 0.0f};
    input.sun_diffuse = {0.9f, 0.8f, 0.7f};
    input.ambient = {0.4f, 0.4f, 0.4f};
    input.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const LightingUniforms plain = ComputeLighting(input, LightingMaterial::Hull(), nullptr, Settings{});
    CHECK(Near(plain.u_fxLight[0][0], 0.6f, 1e-6f) && Near(plain.u_fxLight[0][1], 0.8f, 1e-6f));
    CHECK(Near(plain.u_fxLight[2][1], 1.0f, 1e-6f));
    // Sky and ground ambient average to the flat ambient the legacy pipeline used.
    for (int i = 0; i < 3; ++i)
        CHECK(Near(0.5f * (plain.u_fxLight[3][i] + plain.u_fxLight[4][i]), 0.4f, 1e-5f));
    CHECK(plain.u_fxLight[3][1] > plain.u_fxLight[4][1]);
    CHECK(plain.u_fxLight[0][3] == LightingMaterial::Hull().specular_power);

    // A rotated camera rotates the vectors, not their length.
    input.view = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 5, 6, 7, 1};
    const LightingUniforms rotated = ComputeLighting(input, LightingMaterial::Sail(), nullptr, Settings{});
    CHECK(Near(rotated.u_fxLight[0][2], 0.6f, 1e-6f) && Near(rotated.u_fxLight[0][1], 0.8f, 1e-6f));
    CHECK(rotated.u_fxLight[2][3] == LightingMaterial::Sail().translucency);

    // With a sky the upper hemisphere takes its hue (bluer than the lower one).
    SkyState sky;
    sky.dome = {0.2f, 0.4f, 0.8f};
    const LightingUniforms tinted = ComputeLighting(input, LightingMaterial::Character(), &sky, Settings{});
    // Relief strength: per material, scaled by the preset; stone > wood > canvas, none on characters.
    Settings half;
    half.normal_map_strength = 0.5f;
    CHECK(Near(ComputeLighting(input, LightingMaterial::Stone(), nullptr, half).u_fxLight[5][0], 0.5f, 1e-6f));
    CHECK(LightingMaterial::Stone().relief > LightingMaterial::Hull().relief && LightingMaterial::Hull().relief > LightingMaterial::Sail().relief);
    CHECK(tinted.u_fxLight[5][0] == 0.0f);
    CHECK(tinted.u_fxLight[3][2] > tinted.u_fxLight[3][0]);
    CHECK(tinted.u_fxLight[4][0] > tinted.u_fxLight[4][2]);

    Settings s;
    CHECK(LightingDefines(s).empty() && PointLightBudget(s) == 0);
    s.lighting_flags = kLightingPerPixel | kLightingHemisphere;
    CHECK(LightingDefines(s) == "FX_LIGHT_PER_PIXEL;FX_LIGHT_HEMISPHERE");
    s.lighting_flags = kLightingPoint; // baked geometry: dynamic point lights only
    CHECK(LightingDefines(s) == "FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=4" && PointLightBudget(s) == 4);
    CHECK(LightingDefines(Settings::FromPreset(QualityPreset::Medium)) ==
          "FX_LIGHT_PER_PIXEL;FX_LIGHT_SHADOW;FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=4;FX_LIGHT_NORMALMAP");
    CHECK(LightingDefines(Settings::FromPreset(QualityPreset::High)) ==
          "FX_LIGHT_PER_PIXEL;FX_LIGHT_SPECULAR;FX_LIGHT_TRANSLUCENT;FX_LIGHT_SHADOW;FX_LIGHT_SHADOW_SOFT;"
          "FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=8;FX_LIGHT_NORMALMAP");
    CHECK(PointLightBudget(Settings::FromPreset(QualityPreset::High)) == 8);
}

// Nothing is downgraded silently: every unsupported request is reported and fixed explicitly.
void CheckSupport()
{
    Support all;
    all.lut_3d = all.shadow_map = all.scene_snapshot = all.wave_field = all.scene_depth = all.normal_map = true;
    all.msaa = all.msaa_depth = all.anisotropy = true;
    for (QualityPreset preset : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High})
    {
        const Settings settings = Settings::FromPreset(preset);
        CHECK(ValidateSettings(settings, all).empty());
        std::vector<std::string> changes;
        CHECK(ConstrainSettings(settings, all, changes) == settings && changes.empty());
        // The original look never depends on a capability.
        if (preset == QualityPreset::Low)
            CHECK(ValidateSettings(settings, Support{}).empty());
    }
    const Settings high = Settings::FromPreset(QualityPreset::High);

    // No sampleable scene depth: occlusion off, water one level down (and its refraction with it).
    Support no_depth = all;
    no_depth.scene_depth = false;
    CHECK(ValidateSettings(high, no_depth).size() == 2);
    std::vector<std::string> changes;
    Settings constrained = ConstrainSettings(high, no_depth, changes);
    CHECK(!constrained.ssao && constrained.water == WaterQuality::Medium && !constrained.water_refraction);
    CHECK(changes.size() == 2); // occlusion, water quality (which takes refraction with it)
    CHECK(constrained.shadows == high.shadows && constrained.bloom && constrained.lighting_flags == high.lighting_flags);
    CHECK(ValidateSettings(constrained, no_depth).empty());

    // No shadow maps.
    Support no_shadow = all;
    no_shadow.shadow_map = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_shadow, changes);
    CHECK(constrained.shadows == ShadowQuality::Off && changes.size() == 1);
    CHECK(LightingDefines(constrained).find("FX_LIGHT_SHADOW") == std::string::npos);

    // No 3D textures: the post chain is reduced to its plain copy; MSAA is the renderer's and stays.
    Support no_lut = all;
    no_lut.lut_3d = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_lut, changes);
    CHECK(!constrained.tonemap && !constrained.bloom && !constrained.sun_shafts && !constrained.ssao && !constrained.dither);
    CHECK(constrained.msaa == 4 && constrained.PostIsIdentity());
    CHECK(ValidateSettings(constrained, no_lut).empty());

    // MSAA without depth resolve: the multisampled scene keeps MSAA and gives up what needs depth.
    Support no_msaa_depth = all;
    no_msaa_depth.msaa_depth = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_msaa_depth, changes);
    CHECK(constrained.msaa == 4 && !constrained.ssao && constrained.water == WaterQuality::Medium && changes.size() == 2);
    // No MSAA at all: alpha to coverage goes with it, and then depth-based effects are fine again.
    Support no_msaa = no_msaa_depth;
    no_msaa.msaa = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_msaa, changes);
    CHECK(constrained.msaa == 1 && !constrained.alpha_to_coverage && constrained.ssao && constrained.water == WaterQuality::High);
    const RendererState state = RendererStateFor(high);
    CHECK(state.msaa_samples == 4 && state.alpha_to_coverage && state.max_anisotropy == 8);
    CHECK(RendererStateFor(Settings{}).msaa_samples == 1 && !RendererStateFor(Settings{}).alpha_to_coverage);
    Support no_aniso = all;
    no_aniso.anisotropy = false;
    changes.clear();
    CHECK(ConstrainSettings(high, no_aniso, changes).anisotropy == 1 && changes.size() == 1);

    // No RG8 textures: normal maps go, and so does the shader define.
    Support no_rg = all;
    no_rg.normal_map = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_rg, changes);
    CHECK(changes.size() == 1 && !(constrained.lighting_flags & kLightingNormalMap));
    CHECK(LightingDefines(constrained).find("NORMALMAP") == std::string::npos);
    CHECK(Settings::FromPreset(QualityPreset::Medium).normal_map_strength < high.normal_map_strength);

    // Inconsistent requests are reported as well.
    Settings odd;
    odd.sun_shafts = true; // without bloom
    odd.lighting_flags = kLightingSpecular; // without per-pixel lighting
    odd.shadows = ShadowQuality::Soft;
    odd.water_refraction = true;
    CHECK(ValidateSettings(odd, all).size() == 4);
    changes.clear();
    constrained = ConstrainSettings(odd, all, changes);
    CHECK(changes.size() == 4 && constrained == Settings{});
    CHECK(Support{}.Describe().find("NO") != std::string::npos && all.Describe().find("NO") == std::string::npos);
}

void CheckLights()
{
    PointLight torch;
    torch.position = {0.0f, 2.0f, 0.0f};
    torch.color = {1.0f, 0.6f, 0.3f};
    torch.range = 10.0f;
    torch.att0 = 0.5f;
    torch.att1 = 0.1f;
    torch.att2 = 0.05f;
    // D3D9 attenuation inside the range, nothing beyond it, a short fade right before the limit.
    CHECK(Near(LightAttenuation(torch, 0.0f), 2.0f, 1e-5f));
    CHECK(Near(LightAttenuation(torch, 4.0f), 1.0f / (0.5f + 0.4f + 0.8f), 1e-5f));
    CHECK(LightAttenuation(torch, 10.01f) == 0.0f);
    CHECK(Near(LightAttenuation(torch, 9.25f), 0.5f / (0.5f + 0.925f + 0.05f * 9.25f * 9.25f), 1e-4f));
    float previous = 1e9f;
    for (int i = 0; i <= 100; ++i)
    {
        const float a = LightAttenuation(torch, 0.1f * i);
        CHECK(a <= previous && a >= 0.0f);
        previous = a;
    }

    // Selection: strongest at the object first, unreachable lights dropped, the overflow counted.
    std::vector<PointLight> lights;
    for (int i = 0; i < 12; ++i)
    {
        PointLight light = torch;
        light.position = {3.0f * i, 2.0f, 0.0f};
        light.baked = i % 2 == 0;
        lights.push_back(light);
    }
    const LightSelection four = SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 4);
    CHECK(four.count == 4 && four.dropped == 0); // lights at x = 0, 3, 6, 9 reach; x = 12 and beyond do not
    CHECK(four.lights[0].position[0] == 0.0f && four.lights[3].position[0] == 9.0f);
    const LightSelection crowded = SelectLights(lights, {15.0f, 1.0f, 0.0f}, 6.0f, 4);
    CHECK(crowded.count == 4 && crowded.dropped > 0);
    CHECK(SelectLights(lights, {15.0f, 1.0f, 0.0f}, 6.0f, 8).dropped < crowded.dropped);
    CHECK(SelectLights(lights, {500.0f, 0.0f, 0.0f}, 1.0f, 8).count == 0);
    CHECK(SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 100).count <= kMaxPointLights);
    // Geometry with baked vertex colours: baked lights are left out, dynamic ones stay.
    const LightSelection dynamic = SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 8, false);
    CHECK(dynamic.count == 2 && !dynamic.lights[0].baked && !dynamic.lights[1].baked);
    // A brighter light further away can outrank a dim one nearby.
    lights[3].color = {16.0f, 16.0f, 16.0f};
    CHECK(SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 1).lights[0].position[0] == 9.0f);

    // Uniform packing: view-space position, D3D terms, spot cone as scale and bias.
    std::array<float, 16> view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, -2, -3, 1};
    LightSelection selection;
    selection.count = 2;
    selection.dropped = 3;
    selection.lights[0] = torch;
    selection.lights[1] = torch;
    selection.lights[1].direction = {0.0f, -1.0f, 0.0f};
    selection.lights[1].cos_inner = 0.9f;
    selection.lights[1].cos_outer = 0.7f;
    selection.lights[1].falloff = 2.0f;
    const PointLightUniforms packed = PackPointLights(selection, view);
    CHECK(packed.u_fxPointCount[0] == 2.0f && packed.dropped == 3);
    CHECK(Near(packed.u_fxPoint[0][0], -1.0f, 1e-6f) && Near(packed.u_fxPoint[0][1], 0.0f, 1e-6f) && packed.u_fxPoint[0][3] == 10.0f);
    CHECK(packed.u_fxPoint[1][3] == 0.5f && packed.u_fxPoint[2][0] == 0.1f && packed.u_fxPoint[2][1] == 0.05f);
    CHECK(packed.u_fxPoint[2][2] == 0.0f && packed.u_fxPoint[2][3] == 1.0f); // point light: cone factor is always 1
    const float scale = packed.u_fxPoint[6][2], bias = packed.u_fxPoint[6][3];
    CHECK(Near(0.9f * scale + bias, 1.0f, 1e-4f) && Near(0.7f * scale + bias, 0.0f, 1e-4f) && packed.u_fxPoint[7][3] == 2.0f);

    // Flashes: bright at once, gone after their duration, never more than the pool holds.
    FlashLights flashes;
    flashes.Add({1.0f, 2.0f, 3.0f}, {3.0f, 2.0f, 1.0f}, 20.0f, 0.2f);
    std::vector<PointLight> out;
    flashes.AppendTo(out);
    CHECK(out.size() == 1 && out[0].color[0] == 3.0f && !out[0].baked && out[0].range == 20.0f);
    CHECK(LightAttenuation(out[0], 0.0f) == 1.0f && LightAttenuation(out[0], 19.0f) < 0.06f);
    flashes.Update(0.1f);
    out.clear();
    flashes.AppendTo(out);
    CHECK(Near(out[0].color[0], 0.75f, 1e-4f)); // half the time left: a quarter of the peak
    flashes.Update(0.11f);
    CHECK(flashes.Count() == 0);
    for (int i = 0; i < 50; ++i)
        flashes.Add({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 5.0f, 1.0f);
    CHECK(flashes.Count() == FlashLights::kCapacity);
    flashes.Add({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 5.0f, 0.0f); // nonsense is ignored
    CHECK(flashes.Count() == FlashLights::kCapacity);
}

void CheckShadows()
{
    CHECK(ShadowMapSize(ShadowQuality::Off) == 0 && ShadowMapSize(ShadowQuality::Low) == 1024 && ShadowMapSize(ShadowQuality::Soft) == 2048);
    const Vec3 sun{0.48f, 0.64f, 0.6f};
    const Vec3 focus{100.0f, 5.0f, -40.0f};
    const ShadowSetup setup = ComputeShadowSetup(sun, focus, 50.0f, 120.0f, 1024);
    CHECK(Near(setup.texel_world, 100.0f / 1024.0f, 1e-6f));
    // The focus is at the centre of the map (within a texel), half way through the receiver depth range.
    const auto centre = ShadowCoord(setup, focus);
    CHECK(Near(centre[0], 0.5f, 1.0f / 1024.0f) && Near(centre[1], 0.5f, 1.0f / 1024.0f));
    CHECK(Near(centre[2], 170.0f / 220.0f, 1e-3f));
    // Moving towards the sun: same texel, smaller depth. A caster 100 m up the sun ray is still inside.
    const auto nearer = ShadowCoord(setup, {focus[0] + sun[0] * 100.0f, focus[1] + sun[1] * 100.0f, focus[2] + sun[2] * 100.0f});
    CHECK(Near(nearer[0], centre[0], 1e-3f) && Near(nearer[1], centre[1], 1e-3f) && nearer[2] < centre[2] && nearer[2] > 0.0f);
    // Points on the edge of the sphere stay inside the map.
    for (Vec3 offset : {Vec3{49.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -49.0f}, Vec3{-30.0f, 20.0f, 30.0f}})
    {
        const auto c = ShadowCoord(setup, {focus[0] + offset[0], focus[1] + offset[1], focus[2] + offset[2]});
        CHECK(c[0] > 0.0f && c[0] < 1.0f && c[1] > 0.0f && c[1] < 1.0f && c[2] > 0.0f && c[2] < 1.0f);
    }
    // Texel snapping: a small camera move does not shift the map by a fraction of a texel.
    const ShadowSetup moved = ComputeShadowSetup(sun, {focus[0] + 0.013f, focus[1], focus[2] + 0.021f}, 50.0f, 120.0f, 1024);
    const Vec3 probe{90.0f, 3.0f, -35.0f};
    const float shift_u = (ShadowCoord(moved, probe)[0] - ShadowCoord(setup, probe)[0]) * 1024.0f;
    const float shift_v = (ShadowCoord(moved, probe)[1] - ShadowCoord(setup, probe)[1]) * 1024.0f;
    CHECK(Near(shift_u, std::round(shift_u), 2e-2f) && Near(shift_v, std::round(shift_v), 2e-2f));
    // A sun straight overhead is a valid direction.
    const ShadowSetup zenith = ComputeShadowSetup({0.0f, 1.0f, 0.0f}, focus, 50.0f, 120.0f, 1024);
    CHECK(Near(ShadowCoord(zenith, focus)[0], 0.5f, 1.0f / 1024.0f));
}
// Decodes texel (x, y) of mip `mip` into a unit normal.
std::array<float, 3> NormalAt(const NormalMap &map, int mip, int x, int y)
{
    size_t offset = 0;
    int w = map.width, h = map.height;
    for (int m = 0; m < mip; ++m)
    {
        offset += static_cast<size_t>(w) * h * 2;
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
    }
    const float nx = map.rg[offset + (static_cast<size_t>(y) * w + x) * 2] / 255.0f * 2.0f - 1.0f;
    const float ny = map.rg[offset + (static_cast<size_t>(y) * w + x) * 2 + 1] / 255.0f * 2.0f - 1.0f;
    return {nx, ny, std::sqrt(std::max(1.0f - nx * nx - ny * ny, 0.0f))};
}

void CheckNormalMaps()
{
    const int n = 64;
    NormalMapOptions options;
    options.strength = 4.0f;
    options.pre_blur = false;

    // A flat height field: every normal points straight out, at every mip.
    const std::vector<uint8_t> flat(n * n, 77);
    const NormalMap level = DeriveNormalMap(flat, n, n, options);
    CHECK(level.width == n && level.height == n && level.mip_count == 7);
    size_t texels = 0;
    for (int s = n; s >= 1; s /= 2)
        texels += static_cast<size_t>(s) * s;
    CHECK(level.rg.size() == texels * 2);
    CHECK(std::all_of(level.rg.begin(), level.rg.end(), [](uint8_t v) { return v == 128; }));

    // A ramp along u: slope = strength / width per texel, the normal leans against the slope, y stays 0.
    std::vector<uint8_t> ramp(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            ramp[y * n + x] = static_cast<uint8_t>(x * 255 / (n - 1));
    options.wrap = false;
    const NormalMap slope = DeriveNormalMap(ramp, n, n, options);
    const float expected = 4.0f / (n - 1); // dh/du in texels, times strength
    const auto mid = NormalAt(slope, 0, n / 2, n / 2);
    CHECK(Near(mid[0] / mid[2], -expected, 0.012f) && Near(mid[1], 0.0f, 0.01f));
    // Mips keep the world-space slope: two levels down the lean is the same.
    const auto coarse = NormalAt(slope, 2, n / 8, n / 8);
    CHECK(Near(coarse[0] / coarse[2], -expected, 0.012f) && Near(coarse[1], 0.0f, 0.01f));
    // Rotated ramp: the lean follows v instead.
    std::vector<uint8_t> ramp_v(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            ramp_v[y * n + x] = ramp[x * n + y];
    const auto mid_v = NormalAt(DeriveNormalMap(ramp_v, n, n, options), 0, n / 2, n / 2);
    CHECK(Near(mid_v[1] / mid_v[2], -expected, 0.012f) && Near(mid_v[0], 0.0f, 0.01f));

    // A cone: normals point away from the apex, with equal lean all around.
    std::vector<uint8_t> cone(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            cone[y * n + x] = static_cast<uint8_t>(std::clamp(255.0f - 6.0f * std::hypot(x - 31.5f, y - 31.5f), 0.0f, 255.0f));
    const NormalMap hill = DeriveNormalMap(cone, n, n, options);
    for (int i = 0; i < 16; ++i)
    {
        const float angle = 0.3927f * i;
        const int x = static_cast<int>(std::lround(31.5f + 14.0f * std::cos(angle))), y = static_cast<int>(std::lround(31.5f + 14.0f * std::sin(angle)));
        const auto normal = NormalAt(hill, 0, x, y);
        const float lean = std::hypot(normal[0], normal[1]) / normal[2];
        CHECK(Near(lean, 4.0f * 6.0f / 255.0f, 0.02f));
        CHECK(normal[0] * std::cos(angle) + normal[1] * std::sin(angle) > 0.9f * std::hypot(normal[0], normal[1]));
    }

    // A sine that tiles: analytic slope everywhere, and the wrap seam is as smooth as the inside.
    std::vector<uint8_t> wave(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            wave[y * n + x] = static_cast<uint8_t>(std::lround(127.5f + 100.0f * std::sin(6.2831853f * 2.0f * (x + 0.5f) / n)));
    options.wrap = true;
    const NormalMap tiled = DeriveNormalMap(wave, n, n, options);
    for (int x = 0; x < n; ++x)
    {
        const float slope_here = 4.0f * (100.0f / 255.0f) * (6.2831853f * 2.0f / n) * std::cos(6.2831853f * 2.0f * (x + 0.5f) / n);
        const auto normal = NormalAt(tiled, 0, x, 5);
        CHECK(Near(normal[0] / normal[2], -slope_here, 0.012f));
    }
    CHECK(std::abs(tiled.rg[0] - tiled.rg[(n - 1) * 2]) <= std::abs(tiled.rg[2 * 10] - tiled.rg[2 * 11]) + 1);
    // Without wrapping the same texture has an edge: the border normal differs from the wrapped one.
    options.wrap = false;
    CHECK(DeriveNormalMap(wave, n, n, options).rg[0] != tiled.rg[0]);
    options.wrap = true;

    // The pre-blur softens DXT block noise: the steps between 4x4 blocks give smaller peak slopes.
    std::vector<uint8_t> blocky(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            blocky[y * n + x] = static_cast<uint8_t>(128 + 6 * ((((x / 4) * 7 + (y / 4) * 13) % 5) - 2));
    const auto peak = [&](bool blur) {
        NormalMapOptions o = options;
        o.strength = 8.0f;
        o.pre_blur = blur;
        const NormalMap map = DeriveNormalMap(blocky, n, n, o);
        int worst = 0;
        for (int i = 0; i < n * n * 2; ++i)
            worst = std::max(worst, std::abs(map.rg[i] - 128));
        return worst;
    };
    CHECK(peak(false) > 8 && peak(true) * 10 <= peak(false) * 8);

    // Deterministic and cacheable: same input, same bytes; the key changes with every option.
    CHECK(DeriveNormalMap(cone, n, n, options).rg == DeriveNormalMap(cone, n, n, options).rg);
    NormalMapOptions other = options;
    other.strength = 2.0f;
    CHECK(NormalMapCacheKey(options) == NormalMapCacheKey(options) && NormalMapCacheKey(options) != NormalMapCacheKey(other));
    other = options;
    other.pre_blur = !options.pre_blur;
    CHECK(NormalMapCacheKey(options) != NormalMapCacheKey(other));
    // Non-square textures and bad input.
    const NormalMap strip = DeriveNormalMap(std::vector<uint8_t>(256 * 64, 10), 256, 64, options);
    CHECK(strip.mip_count == 9 && strip.rg.size() == 2u * (256 * 64 + 128 * 32 + 64 * 16 + 32 * 8 + 16 * 4 + 8 * 2 + 4 + 2 + 1));
    CHECK(DeriveNormalMap(flat, n, n + 1, options).rg.empty());
}

void CheckCheapUpgrades()
{
    WeatherInput weather;
    weather.sun_dir = {0.6f, 0.8f, 0.0f};
    weather.fog_density = 0.002f;
    const SkyState noon = ComputeSky(weather);

    // Aerial perspective: the game's fog density, a haze layer that thins with the camera's height, world up
    // taken from the view matrix, haze colour = the horizon (gamma encoded).
    const std::array<float, 16> view = {0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1}; // world y -> view x
    const AerialUniforms low = ComputeAerial(noon, 2.0f, view), high_up = ComputeAerial(noon, 200.0f, view);
    CHECK(low.u_fxAerial[0][3] == 0.002f && Near(low.u_fxAerial[0][1], std::sqrt(noon.horizon[1]), 1e-5f));
    CHECK(low.u_fxAerial[1][0] > 10.0f * high_up.u_fxAerial[1][0] && high_up.u_fxAerial[1][0] > 0.0f);
    CHECK(low.u_fxAerial[2][0] == 1.0f && low.u_fxAerial[2][1] == 0.0f && low.u_fxAerial[1][3] < 1.0f);
    // The emulator's form: a sixth of the haze, relative to the fog density, milder desaturation, no opacity cap.
    const auto fog_low = ComputeAerialFogHaze(2.0f), fog_high = ComputeAerialFogHaze(200.0f);
    CHECK(Near(fog_low[0] * noon.fog_density * 6.0f, low.u_fxAerial[1][0], 1e-7f) && fog_low[0] > 10.0f * fog_high[0]);
    CHECK(fog_low[1] == low.u_fxAerial[1][1] && kAerialFogDesaturation < low.u_fxAerial[1][2]);
    // The sea under the haze: denser than the plain fog, less so from the masthead than from the deck.
    CHECK(SeaFogDensity(noon, 2.0f) > SeaFogDensity(noon, 30.0f) && SeaFogDensity(noon, 30.0f) > noon.fog_density);
    CHECK(SeaFogDensity(noon, 2.0f) < noon.fog_density * (1.0f + kHazeDensityFactor) + 1e-6f);

    // Wind: stronger weather, larger and faster sway; the direction is the weather's.
    weather.wind = {0.6f, 0.8f};
    weather.wind_speed = 3.0f;
    const WindUniforms breeze = ComputeWind(weather, 5.0f);
    weather.wind_speed = 30.0f;
    const WindUniforms gale = ComputeWind(weather, 5.0f);
    CHECK(breeze.u_fxWind[0][0] == 0.6f && breeze.u_fxWind[0][2] == 0.8f && breeze.u_fxWind[0][3] == 5.0f);
    CHECK(gale.u_fxWind[1][0] > 2.0f * breeze.u_fxWind[1][0] && gale.u_fxWind[1][1] > breeze.u_fxWind[1][1]);
    CHECK(gale.u_fxWind[1][0] <= 0.5f + 1e-6f); // saturates: trees do not fly away in a storm

    // Scene grades: each mood is distinct, the blend follows the sky continuously.
    const auto sky_at = [](float sun_height) {
        WeatherInput w;
        w.sun_dir = {std::sqrt(1.0f - sun_height * sun_height), sun_height, 0.0f};
        return ComputeSky(w);
    };
    const std::array<float, 3> grey{0.5f, 0.5f, 0.5f};
    const ColorGrade day = SceneGrade(sky_at(0.8f), 0.0f, 0.0f), dusk = SceneGrade(sky_at(0.08f), 0.0f, 0.0f);
    const ColorGrade night = SceneGrade(sky_at(-0.5f), 0.0f, 0.0f), storm = SceneGrade(sky_at(0.8f), 1.0f, 0.0f);
    const ColorGrade indoors = SceneGrade(sky_at(-0.5f), 0.0f, 1.0f);
    CHECK(day == ColorGrade::ForMood(ColorGrade::Mood::TropicalNoon) && night == ColorGrade::ForMood(ColorGrade::Mood::Night));
    CHECK(indoors == ColorGrade::ForMood(ColorGrade::Mood::Interior));
    CHECK(dusk.Apply(grey)[0] > dusk.Apply(grey)[2] + 0.015f);   // warm
    CHECK(night.Apply(grey)[2] > night.Apply(grey)[0] + 0.015f); // cool
    CHECK(storm.saturation < day.saturation && night.saturation < day.saturation);
    // A small step of the sun is a small step of the grade (no jump at dusk).
    float worst = 0.0f;
    for (int i = 0; i < 60; ++i)
    {
        const auto a = SceneGrade(sky_at(-0.3f + 0.01f * i), 0.0f, 0.0f).Apply(grey), b = SceneGrade(sky_at(-0.29f + 0.01f * i), 0.0f, 0.0f).Apply(grey);
        worst = std::max({worst, std::fabs(a[0] - b[0]), std::fabs(a[2] - b[2])});
    }
    CHECK(worst < 0.02f);
    // The post chain rebuilds the table only when the drift shows in 8-bit output.
    CHECK(GradeDifference(day, day) == 0.0f && GradeDifference(day, night) > 0.02f);
    const ColorGrade user = ColorGrade::Preset(GradePreset::Caribbean);
    // Built two slices at a time (the post chain's background rebuild), the table is the same.
    std::vector<uint8_t> sliced(8 * 8 * 8 * 4);
    for (int first = 0; first < 8; first += 2)
        BuildLutSlices(user, 8, dusk, first, 2, sliced.data());
    CHECK(sliced == BuildLutRgba8(user, 8, dusk));
    CHECK(GradeDifference(SceneGrade(sky_at(0.5f), 0.0f, 0.0f), SceneGrade(sky_at(0.501f), 0.0f, 0.0f)) < 0.5f / 255.0f);
    // The scene grade is applied before the player's grade.
    const std::vector<uint8_t> both = BuildLutRgba8(user, 8, night);
    const auto expected = user.Apply(night.Apply({1.0f / 7.0f, 3.0f / 7.0f, 5.0f / 7.0f}));
    const uint8_t *texel = &both[((5 * 8 + 3) * 8 + 1) * 4];
    CHECK(std::abs(texel[0] - static_cast<int>(std::lround(expected[0] * 255.0f))) <= 1 &&
          std::abs(texel[2] - static_cast<int>(std::lround(expected[2] * 255.0f))) <= 1);
}

int g_log_lines = 0;

void CheckHeightMapRules()
{
    CHECK(NormalizeTexturePath("Locations\\Bump_City.tga.tx") == "locations/bump_city");
    CHECK(NameSuggestsHeightMap("bump_houses") && NameSuggestsHeightMap("ships/sailbumpu1") && NameSuggestsHeightMap("sph2_bump2"));
    CHECK(!NameSuggestsHeightMap("bumpy_folder/wall") && !NameSuggestsHeightMap("deckplanksu2"));

    // Grey with the channel error of DXT compression is grey; a tinted texture is not.
    std::vector<uint8_t> grey, tinted;
    for (int i = 0; i < 1024; ++i)
    {
        const uint8_t v = static_cast<uint8_t>(i % 251);
        grey.insert(grey.end(), {v, static_cast<uint8_t>(std::min(v + (i % 5), 255)), static_cast<uint8_t>(std::max(v - (i % 7), 0)), 255});
        tinted.insert(tinted.end(), {v, static_cast<uint8_t>(v / 2), static_cast<uint8_t>(v / 3), 255});
    }
    CHECK(IsGrayscale(grey) && !IsGrayscale(tinted) && !IsGrayscale({}));

    const HeightMapManifest manifest = HeightMapManifest::Parse("# overrides\n+Detail\\DetailWoodU1.tga\n-bump_land  # looks wrong\n\nnonsense\n");
    CHECK(manifest.include.size() == 1 && manifest.include[0] == "detail/detailwoodu1");
    CHECK(manifest.exclude.size() == 1 && manifest.exclude[0] == "bump_land");

    SetLogSink([](LogLevel, const char *) { ++g_log_lines; });
    HeightMapClassifier classifier(manifest);
    const auto verdict = [&](const char *path, const std::vector<uint8_t> &pixels) { return classifier.Classify(path, pixels); };
    CHECK(verdict("bump_city.tga.tx", grey).is_height_map && verdict("bump_city.tga.tx", grey).reason == HeightMapReason::NameAndGrayscale);
    CHECK(!verdict("Bump3.tga", tinted).is_height_map && verdict("Bump3.tga", tinted).reason == HeightMapReason::NotGrayscale);
    CHECK(verdict("WEATHER\\SEA\\BUMP_G2MX\\seabump0017.tga.tx", grey).reason == HeightMapReason::ExcludedFolder);
    CHECK(verdict("deckPlanksU2.tga", grey).reason == HeightMapReason::NameNoMatch);
    CHECK(verdict("bump_land.tga", grey).reason == HeightMapReason::ManifestExclude && !verdict("bump_land.tga", grey).is_height_map);
    CHECK(verdict("detail/DetailWoodU1.tga", tinted).is_height_map); // the manifest wins over both rules
    // One log line per decided texture, none for ordinary textures, none for repeats.
    CHECK(g_log_lines == 5);
    SetLogSink(nullptr);
}
} // namespace

// Every preset on every GPU-family profile: constrained settings are valid, every loss is reported, and the
// low preset never needs anything.
void CheckCapsProfiles()
{
    Support all;
    all.lut_3d = all.shadow_map = all.scene_snapshot = all.wave_field = all.scene_depth = all.normal_map = true;
    all.msaa = all.msaa_depth = all.anisotropy = true;
    for (int p = 0; p < static_cast<int>(CapsProfile::Count); ++p)
    {
        const auto profile = static_cast<CapsProfile>(p);
        const Support support = RestrictSupport(all, profile);
        CHECK(std::string(CapsProfileName(profile)) != "?");
        for (QualityPreset preset : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High})
        {
            const Settings requested = Settings::FromPreset(preset);
            std::vector<std::string> changes;
            const Settings constrained = ConstrainSettings(requested, support, changes);
            CHECK(ValidateSettings(constrained, support).empty());
            CHECK(changes.empty() == (constrained == requested));
            if (preset == QualityPreset::Low)
                CHECK(changes.empty());
            if (!support.msaa)
                CHECK(constrained.msaa == 1 && !constrained.alpha_to_coverage);
        }
    }
    // A profile only removes.
    CHECK(RestrictSupport(Support{}, CapsProfile::Xclipse).msaa == false);
}

int main()
{
    CheckSettings();
    CheckLut();
    CheckToneCurve();
    CheckNoise();
    CheckEngineWaves();
    CheckGerstner();
    CheckGrid();
    CheckSky();
    CheckLighting();
    CheckSupport();
    CheckCapsProfiles();
    CheckLights();
    CheckShadows();
    CheckNormalMaps();
    CheckCheapUpgrades();
    CheckHeightMapRules();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fx core checks passed\n");
    return 0;
}
