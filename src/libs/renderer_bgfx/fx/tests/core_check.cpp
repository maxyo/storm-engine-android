// Unit checks for the CPU side of the fx package: grading LUTs, tone curve,
// baked noise, the engine wave model and its GPU/Gerstner mappings, the sea
// grid, the sky model and the lighting uniforms. No GPU is needed.
#include "storm/fx/color_lut.hpp"
#include "storm/fx/lighting_model.hpp"
#include "storm/fx/noise_bake.hpp"
#include "storm/fx/settings.hpp"
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
    const Settings original = Settings::FromPreset(QualityPreset::Original);
    CHECK(original.PostIsIdentity());
    CHECK(original.water == WaterQuality::Legacy);
    CHECK(!original.sky_atmosphere);
    CHECK(original.lighting_flags == kLightingNone);
    const Settings medium = Settings::FromPreset(QualityPreset::Medium);
    const Settings high = Settings::FromPreset(QualityPreset::High);
    CHECK(!medium.PostIsIdentity() && !high.PostIsIdentity());
    CHECK(medium.bloom_downscale == 4 && high.bloom_downscale == 2);
    CHECK(!medium.sharpen && high.sharpen);
    CHECK(high.water == WaterQuality::High && medium.water == WaterQuality::Medium);
    CHECK((medium.lighting_flags & kLightingPerPixel) && !(medium.lighting_flags & kLightingSpecular));
    Settings graded = original;
    graded.grade = GradePreset::Caribbean;
    CHECK(!graded.PostIsIdentity());
    graded.grade_strength = 0.0f;
    CHECK(graded.PostIsIdentity());
    CHECK(NameRu(QualityPreset::High) == "Высокое" && NameRu(GradePreset::Overcast) == "Пасмурно");
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
                const auto expect = [n](int i) { return static_cast<int>(std::lround(i * 255.0f / (n - 1))); };
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
    const LightingUniforms plain = ComputeLighting(input, LightingMaterial::Hull(), nullptr);
    CHECK(Near(plain.u_fxLight[0][0], 0.6f, 1e-6f) && Near(plain.u_fxLight[0][1], 0.8f, 1e-6f));
    CHECK(Near(plain.u_fxLight[2][1], 1.0f, 1e-6f));
    // Sky and ground ambient average to the flat ambient the legacy pipeline used.
    for (int i = 0; i < 3; ++i)
        CHECK(Near(0.5f * (plain.u_fxLight[3][i] + plain.u_fxLight[4][i]), 0.4f, 1e-5f));
    CHECK(plain.u_fxLight[3][1] > plain.u_fxLight[4][1]);
    CHECK(plain.u_fxLight[0][3] == LightingMaterial::Hull().specular_power);

    // A rotated camera rotates the vectors, not their length.
    input.view = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 5, 6, 7, 1};
    const LightingUniforms rotated = ComputeLighting(input, LightingMaterial::Sail(), nullptr);
    CHECK(Near(rotated.u_fxLight[0][2], 0.6f, 1e-6f) && Near(rotated.u_fxLight[0][1], 0.8f, 1e-6f));
    CHECK(rotated.u_fxLight[2][3] == LightingMaterial::Sail().translucency);

    // With a sky the upper hemisphere takes its hue (bluer than the lower one).
    SkyState sky;
    sky.dome = {0.2f, 0.4f, 0.8f};
    const LightingUniforms tinted = ComputeLighting(input, LightingMaterial::Character(), &sky);
    CHECK(tinted.u_fxLight[3][2] > tinted.u_fxLight[3][0]);
    CHECK(tinted.u_fxLight[4][0] > tinted.u_fxLight[4][2]);

    CHECK(LightingDefines(kLightingNone).empty());
    CHECK(LightingDefines(kLightingSpecular).empty());
    CHECK(LightingDefines(kLightingPerPixel | kLightingHemisphere) == "FX_LIGHT_PER_PIXEL;FX_LIGHT_HEMISPHERE");
    CHECK(LightingDefines(kLightingAll) == "FX_LIGHT_PER_PIXEL;FX_LIGHT_HEMISPHERE;FX_LIGHT_SPECULAR;FX_LIGHT_TRANSLUCENT");
}
} // namespace

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
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fx core checks passed\n");
    return 0;
}
