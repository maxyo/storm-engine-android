#include "scene.hpp"

#include "storm/fx/noise_bake.hpp"

#include <algorithm>
#include <cmath>

namespace preview
{
namespace
{
constexpr float kPi = 3.14159265358979f;

uint32_t Pack(Color c, float shade = 1.0f)
{
    const auto byte = [shade](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v * shade, 0.0f, 1.0f) * 255.0f)); };
    return 0xff000000u | (byte(c[2]) << 16) | (byte(c[1]) << 8) | byte(c[0]);
}

struct V3
{
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Normalize(V3 a)
{
    const float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return len > 1e-9f ? a * (1.0f / len) : V3{0.0f, 1.0f, 0.0f};
}

// A (u, v) grid surface; normals from finite differences of the same function.
template <typename Surface> Mesh MakeSurface(int nu, int nv, Surface &&surface, Color color, bool flip = false)
{
    Mesh mesh;
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i)
        {
            const float u = static_cast<float>(i) / nu, v = static_cast<float>(j) / nv;
            const float e = 1e-3f;
            const V3 p = surface(u, v);
            V3 n = Normalize(Cross(surface(u, std::min(v + e, 1.0f)) - surface(u, std::max(v - e, 0.0f)),
                                   surface(std::min(u + e, 1.0f), v) - surface(std::max(u - e, 0.0f), v)));
            if (flip)
                n = n * -1.0f;
            mesh.vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, Pack(color)});
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i)
        {
            const uint32_t a = j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    return mesh;
}

Mesh MakeCylinder(V3 base, V3 top, float r0, float r1, Color color)
{
    const V3 axis = Normalize(top - base);
    const V3 side = Normalize(Cross(axis, std::fabs(axis.y) > 0.9f ? V3{1.0f, 0.0f, 0.0f} : V3{0.0f, 1.0f, 0.0f}));
    const V3 other = Cross(axis, side);
    return MakeSurface(
        12, 1,
        [=](float u, float v) {
            const float a = u * 2.0f * kPi, r = r0 + (r1 - r0) * v;
            return base + (top - base) * v + side * (std::cos(a) * r) + other * (std::sin(a) * r);
        },
        color);
}

void Transform(Mesh &mesh, std::array<float, 3> origin, float heading)
{
    const float c = std::cos(heading), s = std::sin(heading);
    for (Vertex &v : mesh.vertices)
    {
        const float x = v.pos[0], z = v.pos[2], nx = v.normal[0], nz = v.normal[2];
        v.pos[0] = origin[0] + x * c + z * s;
        v.pos[1] += origin[1];
        v.pos[2] = origin[2] - x * s + z * c;
        v.normal[0] = nx * c + nz * s;
        v.normal[2] = -nx * s + nz * c;
    }
}
} // namespace

Harbour MakeHarbour()
{
    Harbour harbour;
    const Color stone{0.46f, 0.43f, 0.39f}, plaster{0.62f, 0.56f, 0.47f}, roof{0.36f, 0.20f, 0.15f}, wood{0.25f, 0.18f, 0.12f};
    // Quay: a finely tessellated slab, so the baked vertex lighting has something to interpolate.
    harbour.town.Append(MakeSurface(
        96, 48, [](float u, float v) { return V3{(u * 2.0f - 1.0f) * 60.0f, 2.4f, 8.0f + v * 60.0f}; }, stone));
    harbour.town.Append(MakeSurface(
        96, 4, [](float u, float v) { return V3{(u * 2.0f - 1.0f) * 60.0f, 2.4f - v * 6.0f, 8.0f}; }, stone, true));
    const auto torch = [&](V3 p) {
        storm::fx::PointLight light;
        light.position = {p.x, p.y, p.z};
        light.color = {1.0f, 0.60f, 0.26f};
        light.range = 16.0f;
        light.att0 = 0.35f;
        light.att1 = 0.10f;
        light.att2 = 0.035f;
        light.baked = true;
        harbour.lights.push_back(light);
        harbour.glow.Append(MakeSphere({p.x, p.y, p.z}, 0.16f, {1.0f, 0.78f, 0.36f}, 6));
        harbour.town.Append(MakeBox({p.x, p.y - 0.45f, p.z + 0.12f}, {0.04f, 0.32f, 0.04f}, wood));
    };
    const auto lantern = [&](V3 p) {
        storm::fx::PointLight light;
        light.position = {p.x, p.y, p.z};
        light.color = {0.95f, 0.82f, 0.52f};
        light.range = 11.0f;
        light.att0 = 0.5f;
        light.att1 = 0.12f;
        light.att2 = 0.06f;
        light.baked = true;
        harbour.lights.push_back(light);
        harbour.glow.Append(MakeBox({p.x, p.y, p.z}, {0.11f, 0.15f, 0.11f}, {1.0f, 0.92f, 0.66f}));
        harbour.town.Append(MakeBox({p.x, 2.4f + 0.5f * (p.y - 2.6f), p.z}, {0.06f, 0.5f * (p.y - 2.6f), 0.06f}, wood));
    };
    for (int i = 0; i < 9; ++i)
    {
        const float x = -52.0f + 13.0f * i, front = 24.0f + 3.0f * (i % 3), width = 5.2f, height = 6.0f + 1.6f * (i % 4);
        // Front wall as a grid (baked lighting), the rest as a box, a darker roof on top.
        harbour.town.Append(MakeSurface(
            14, 12, [=](float u, float v) { return V3{x + (u * 2.0f - 1.0f) * width, 2.4f + v * height, front}; }, plaster, true));
        harbour.town.Append(MakeBox({x, 2.4f + 0.5f * height, front + 4.05f}, {width, 0.5f * height, 4.0f}, plaster));
        harbour.town.Append(MakeBox({x, 2.4f + height + 0.35f, front + 3.8f}, {width + 0.4f, 0.35f, 4.5f}, roof));
        harbour.town.Append(MakeBox({x - 0.2f, 3.5f, front - 0.03f}, {0.6f, 1.1f, 0.03f}, wood)); // door
        for (int w = 0; w < 1 + i % 3; ++w)
            harbour.glow.Append(MakeBox({x - 2.8f + 2.6f * w, 5.6f + 1.9f * ((w + i) % 2), front - 0.03f}, {0.5f, 0.7f, 0.03f},
                                        {1.0f, 0.80f, 0.42f}));
        torch({x + 1.6f, 4.9f, front - 0.35f});
        if (i % 2 == 0)
            torch({x - 3.6f, 4.7f, front - 0.35f});
        lantern({x + 6.5f, 5.4f, 11.5f + 2.0f * (i % 2)});
    }
    // Crates and barrels to catch the light.
    for (int i = 0; i < 6; ++i)
        harbour.town.Append(MakeBox({-20.0f + 7.5f * i, 2.4f + 0.55f, 15.0f + 2.5f * (i % 3)}, {0.7f, 0.55f, 0.6f}, wood));
    BakeLighting(harbour.town, harbour.lights, {0.085f, 0.10f, 0.15f});
    return harbour;
}

void BakeLighting(Mesh &mesh, const std::vector<storm::fx::PointLight> &lights, Color ambient)
{
    for (Vertex &v : mesh.vertices)
    {
        Color sum = ambient;
        for (const storm::fx::PointLight &light : lights)
        {
            const V3 to{light.position[0] - v.pos[0], light.position[1] - v.pos[1], light.position[2] - v.pos[2]};
            const float dist = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
            const float ndl = std::max((to.x * v.normal[0] + to.y * v.normal[1] + to.z * v.normal[2]) / std::max(dist, 1e-4f), 0.0f);
            const float k = ndl * storm::fx::LightAttenuation(light, dist);
            for (int c = 0; c < 3; ++c)
                sum[c] += light.color[c] * k;
        }
        v.baked = Pack(sum);
    }
}

void Mesh::Append(const Mesh &other)
{
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    for (uint32_t index : other.indices)
        indices.push_back(base + index);
}

Mesh MakeSphere(std::array<float, 3> centre, float radius, Color color, int rings)
{
    return MakeSurface(
        rings * 2, rings,
        [=](float u, float v) {
            const float a = u * 2.0f * kPi, b = (v - 0.5f) * kPi;
            return V3{centre[0] + radius * std::cos(b) * std::cos(a), centre[1] + radius * std::sin(b),
                      centre[2] + radius * std::cos(b) * std::sin(a)};
        },
        color);
}

Mesh MakeBox(std::array<float, 3> c, std::array<float, 3> h, Color color)
{
    Mesh mesh;
    const float axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto &n : axes)
    {
        const V3 normal{n[0], n[1], n[2]};
        const V3 u = std::fabs(n[1]) > 0.5f ? V3{1.0f, 0.0f, 0.0f} : V3{0.0f, 1.0f, 0.0f};
        const V3 v = Cross(normal, u);
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i)
        {
            const float su = (i & 1) ? 1.0f : -1.0f, sv = (i & 2) ? 1.0f : -1.0f;
            const V3 p = normal + u * su + v * sv;
            mesh.vertices.push_back({{c[0] + p.x * h[0], c[1] + p.y * h[1], c[2] + p.z * h[2]}, {n[0], n[1], n[2]}, Pack(color)});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    }
    return mesh;
}

Ship MakeShip(std::array<float, 3> origin, float heading)
{
    const Color hull{0.36f, 0.23f, 0.13f}, trim{0.62f, 0.50f, 0.22f}, deck{0.55f, 0.43f, 0.28f}, canvas{0.86f, 0.82f, 0.70f},
        spar{0.30f, 0.21f, 0.13f};
    const float length = 30.0f, beam = 4.2f, draught = 2.6f, freeboard = 3.6f;
    Mesh ship, sails;
    // Hull: half-tube whose width follows the plan of a galleon, with a raised stern.
    ship.Append(MakeSurface(
        40, 14,
        [=](float u, float v) {
            const float t = u * 2.0f - 1.0f; // stern -1 .. bow 1
            const float width = beam * std::pow(std::max(1.0f - std::pow(std::fabs(t), 2.6f), 0.0f), 0.55f) * (t < 0.0f ? 1.0f : 0.92f);
            const float sheer = freeboard + 2.2f * std::max(-t - 0.45f, 0.0f) + 1.0f * std::max(t - 0.7f, 0.0f);
            const float a = (v - 0.5f) * kPi; // port .. keel .. starboard
            const float side = std::sin(a), down = std::cos(a);
            const float y = down > 0.0f ? -draught * std::pow(down, 0.8f) : 0.0f;
            return V3{t * length * 0.5f, y + sheer * std::pow(std::fabs(side), 3.0f), width * side * (0.72f + 0.28f * std::fabs(side))};
        },
        hull, true));
    // Deck and a gilded rail band.
    ship.Append(MakeSurface(
        24, 2,
        [=](float u, float v) {
            const float t = u * 2.0f - 1.0f;
            const float width = 0.9f * beam * std::pow(std::max(1.0f - std::pow(std::fabs(t), 2.6f), 0.0f), 0.55f);
            const float sheer = freeboard + 2.2f * std::max(-t - 0.45f, 0.0f) + 1.0f * std::max(t - 0.7f, 0.0f);
            return V3{t * length * 0.5f, sheer - 0.35f, (v * 2.0f - 1.0f) * width};
        },
        deck));
    ship.Append(MakeBox({-11.5f, freeboard + 1.6f, 0.0f}, {3.2f, 1.2f, 3.1f}, trim));
    // Masts, yards and square sails that belly forward.
    const float mast_x[3] = {-7.5f, 1.0f, 9.0f}, mast_h[3] = {22.0f, 27.0f, 21.0f};
    for (int m = 0; m < 3; ++m)
    {
        const V3 foot{mast_x[m], freeboard - 0.5f, 0.0f};
        ship.Append(MakeCylinder(foot, foot + V3{0.0f, mast_h[m], 0.0f}, 0.42f, 0.2f, spar));
        const int sail_count = m == 1 ? 3 : 2;
        float y0 = freeboard + 4.5f;
        for (int k = 0; k < sail_count; ++k)
        {
            const float height = (mast_h[m] - 6.0f) / sail_count - 0.8f;
            const float half = (6.6f - 1.3f * k) * (m == 1 ? 1.0f : 0.85f);
            const float x = mast_x[m] + 0.5f;
            ship.Append(MakeCylinder({x, y0 + height, -half - 0.4f}, {x, y0 + height, half + 0.4f}, 0.16f, 0.16f, spar));
            sails.Append(MakeSurface(
                10, 8,
                [=](float u, float v) {
                    const float s = u * 2.0f - 1.0f;
                    const float belly = 1.5f * (1.0f - s * s) * std::sin(v * kPi) + 0.25f * std::sin(v * kPi);
                    return V3{x + belly, y0 + v * height, s * half * (0.92f + 0.08f * v)};
                },
                canvas));
            y0 += height + 0.8f;
        }
    }
    ship.Append(MakeCylinder({13.5f, freeboard + 0.6f, 0.0f}, {21.0f, freeboard + 3.6f, 0.0f}, 0.28f, 0.12f, spar));
    Transform(ship, origin, heading);
    Transform(sails, origin, heading);
    return {ship, sails};
}

Mesh MakeIsland(std::array<float, 3> centre, float radius, float height)
{
    const Color sand{0.80f, 0.72f, 0.52f}, grass{0.26f, 0.42f, 0.18f}, rock{0.42f, 0.38f, 0.33f};
    Mesh mesh = MakeSurface(
        64, 64,
        [=](float u, float v) {
            const float x = (u * 2.0f - 1.0f), z = (v * 2.0f - 1.0f);
            const float r = std::sqrt(x * x + z * z);
            const float noise = storm::fx::PeriodicFbm(u, v, 3, 3, 77u);
            // A dome that slides gently under the water: beaches first, a rocky hill in the middle.
            float h = (1.0f - r * (1.12f - 0.25f * noise));
            h = h > 0.0f ? std::pow(h, 1.5f) * (1.0f + 0.9f * noise) : h * 0.35f;
            return V3{centre[0] + x * radius, centre[1] - 1.5f + h * height, centre[2] + z * radius};
        },
        sand);
    for (Vertex &v : mesh.vertices)
    {
        const float h = v.pos[1] - centre[1];
        const float slope = 1.0f - v.normal[1];
        Color c = sand;
        if (h > 2.2f)
            c = grass;
        if (slope > 0.28f && h > 1.2f)
            c = rock;
        if (h < 0.0f)
            c = {0.62f, 0.58f, 0.45f};
        const float grain = 0.9f + 0.2f * storm::fx::PeriodicFbm(v.pos[0] * 0.013f, v.pos[2] * 0.013f, 16, 3, 5u);
        v.abgr = Pack(c, grain);
    }
    return mesh;
}

Mesh MakeFigure(std::array<float, 3> feet)
{
    const Color skin{0.82f, 0.62f, 0.50f}, coat{0.48f, 0.12f, 0.12f}, breeches{0.20f, 0.20f, 0.26f}, hat{0.12f, 0.10f, 0.09f};
    Mesh figure;
    const V3 f{feet[0], feet[1], feet[2]};
    for (float side : {-0.13f, 0.13f})
        figure.Append(MakeCylinder(f + V3{side, 0.0f, 0.0f}, f + V3{side, 0.92f, 0.0f}, 0.085f, 0.11f, breeches));
    // Torso: a squashed, tapered tube.
    figure.Append(MakeSurface(
        20, 8,
        [=](float u, float v) {
            const float a = u * 2.0f * kPi;
            const float r = 0.19f + 0.07f * std::sin(v * kPi) + 0.03f * v;
            return f + V3{std::cos(a) * r * 1.25f, 0.9f + v * 0.62f, std::sin(a) * r * 0.8f};
        },
        coat));
    for (float side : {-1.0f, 1.0f})
        figure.Append(MakeCylinder(f + V3{side * 0.30f, 1.46f, 0.0f}, f + V3{side * 0.40f, 0.92f, 0.08f}, 0.075f, 0.06f, coat));
    figure.Append(MakeSphere({feet[0], feet[1] + 1.68f, feet[2]}, 0.125f, skin, 12));
    figure.Append(MakeCylinder(f + V3{0.0f, 1.76f, 0.0f}, f + V3{0.0f, 1.80f, 0.0f}, 0.26f, 0.26f, hat));
    figure.Append(MakeCylinder(f + V3{0.0f, 1.78f, 0.0f}, f + V3{0.0f, 1.92f, 0.0f}, 0.13f, 0.11f, hat));
    return figure;
}

Mesh MakeAliasingPattern()
{
    Mesh mesh;
    const Color white{1.0f, 1.0f, 1.0f}, dark{0.06f, 0.07f, 0.10f}, red{0.85f, 0.15f, 0.10f}, gold{0.95f, 0.78f, 0.25f};
    mesh.Append(MakeBox({0.0f, 0.0f, 0.5f}, {9.0f, 5.5f, 0.05f}, dark));
    const auto bar = [&](float x0, float y0, float x1, float y1, float width, Color color) {
        const V3 a{x0, y0, 0.0f}, b{x1, y1, 0.0f};
        const V3 side = Normalize(Cross(b - a, V3{0.0f, 0.0f, 1.0f})) * (0.5f * width);
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const V3 &p : {a - side, a + side, b - side, b + side})
            mesh.vertices.push_back({{p.x, p.y, p.z}, {0.0f, 0.0f, -1.0f}, Pack(color)});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    };
    // Fan of thin lines at shallow angles: rigging against the sky.
    for (int i = 0; i < 26; ++i)
    {
        const float angle = 0.02f + 0.06f * i;
        bar(-8.2f, -4.6f, -8.2f + 7.6f * std::cos(angle), -4.6f + 7.6f * std::sin(angle), 0.035f, white);
    }
    // Nearly horizontal and nearly vertical edges of solid shapes: hull and mast silhouettes.
    for (int i = 0; i < 6; ++i)
    {
        bar(0.5f, -4.4f + 0.75f * i, 7.8f, -4.25f + 0.75f * i + 0.07f * i, 0.32f, i % 2 ? red : gold);
        bar(0.9f + 1.2f * i, 0.6f, 1.0f + 1.2f * i + 0.05f * i, 4.8f, 0.3f, white);
    }
    // Bars of decreasing width down to sub-pixel size.
    for (int i = 0; i < 14; ++i)
        bar(-8.0f + 0.52f * i, 1.2f, -7.6f + 0.52f * i, 4.8f, 0.16f / (1.0f + 0.35f * i), gold);
    return mesh;
}

storm::fx::EngineSeaState MakeSeaState(float time)
{
    storm::fx::EngineSeaState sea;
    sea.layers[0] = {2.6f, 0.5f, {4.0f * time, 0.0f}};
    sea.layers[1] = {0.55f, 2.0f, {0.0f, 1.5f * time}};
    sea.pos_shift = 1.2f;
    sea.bump_scale = 0.09f;
    return sea;
}

void FillWaveField(storm::fx::EngineWaveField &field, float time)
{
    const int n = storm::fx::EngineWaveField::kSize;
    for (int layer = 0; layer < 2; ++layer)
    {
        std::vector<float> heights(static_cast<size_t>(n) * n), normals(static_cast<size_t>(n) * n * 2);
        const float phase = time * (layer == 0 ? 0.9f : 1.7f);
        const auto h = [&](int x, int z) {
            const float u = 2.0f * kPi * static_cast<float>(x & (n - 1)) / n, v = 2.0f * kPi * static_cast<float>(z & (n - 1)) / n;
            // Tiling swell: a few integer-frequency waves with sharpened crests plus fractal chop.
            float sum = 0.0f;
            const float waves[5][3] = {{3, 1, 1.0f}, {2, -3, 0.55f}, {5, 2, 0.4f}, {-4, 5, 0.28f}, {7, -6, 0.18f}};
            for (int i = 0; i < 5; ++i)
            {
                const float s = std::sin(waves[i][0] * u + waves[i][1] * v + phase * (1.0f + 0.37f * i) + 1.3f * i + 2.0f * layer);
                sum += waves[i][2] * (1.0f - std::pow(std::fabs(s), 1.3f));
            }
            sum /= 2.41f;
            sum += 0.10f * storm::fx::PeriodicFbm(static_cast<float>(x & (n - 1)) / n, static_cast<float>(z & (n - 1)) / n, 8, 3, 17u + layer);
            return std::clamp(0.5f + 0.5f * (sum - 0.5f) * 1.5f, 0.0f, 1.0f);
        };
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
            {
                heights[z * n + x] = h(x, z);
                // sea.cpp: unit normal of the 0..1 height field with a texel as the horizontal unit.
                const float dx = 0.5f * (h(x + 1, z) - h(x - 1, z)), dz = 0.5f * (h(x, z + 1) - h(x, z - 1));
                const float len = std::sqrt(dx * dx + 1.0f + dz * dz);
                normals[2 * (z * n + x) + 0] = -dx / len;
                normals[2 * (z * n + x) + 1] = -dz / len;
            }
        field.SetLayer(layer, heights, normals);
    }
}

storm::fx::WeatherInput MakeWeather(Weather weather, float sun_turn)
{
    storm::fx::WeatherInput w;
    const auto sun = [sun_turn](float azimuth_deg, float height) {
        const float a = (azimuth_deg + sun_turn) * kPi / 180.0f, c = std::sqrt(1.0f - height * height);
        return storm::fx::Vec3{c * std::sin(a), height, c * std::cos(a)};
    };
    w.wind = {0.8f, 0.6f};
    switch (weather)
    {
    case Weather::Day:
        w.sun_dir = sun(18.0f, 0.60f);
        w.sun_color = {0.86f, 0.82f, 0.78f};
        w.ambient_color = {0.37f, 0.37f, 0.35f};
        w.fog_color = {0.45f, 0.60f, 0.72f};
        w.fog_density = 0.0006f;
        break;
    case Weather::Sunset:
        w.sun_dir = sun(8.0f, 0.085f);
        w.sun_color = {0.95f, 0.66f, 0.42f};
        w.ambient_color = {0.30f, 0.27f, 0.30f};
        w.fog_color = {0.62f, 0.45f, 0.40f};
        w.fog_density = 0.0008f;
        break;
    case Weather::Storm:
        w.sun_dir = sun(40.0f, 0.55f);
        w.sun_color = {0.24f, 0.24f, 0.24f};
        w.ambient_color = {0.22f, 0.23f, 0.24f};
        w.fog_color = {0.20f, 0.24f, 0.26f};
        w.fog_density = 0.0030f;
        w.overcast = 1.0f;
        w.wind_speed = 18.0f;
        break;
    case Weather::Night:
        w.sun_dir = sun(200.0f, -0.45f);
        w.sun_color = {0.10f, 0.14f, 0.22f};
        w.ambient_color = {0.10f, 0.12f, 0.18f};
        w.fog_color = {0.03f, 0.05f, 0.09f};
        w.fog_density = 0.0012f;
        break;
    }
    return w;
}

} // namespace preview
