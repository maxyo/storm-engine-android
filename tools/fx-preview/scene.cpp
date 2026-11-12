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
            mesh.vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, Pack(color), 0xffffffffu, {u, v}});
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

namespace
{
// Quad a-b-c-d (a, b at the anchored end); `tip` is the sway weight of c and d, `base` that of a and b.
void AddCard(Mesh &mesh, V3 a, V3 b, V3 c, V3 d, Color color, float base, float tip)
{
    const V3 n = Normalize(Cross(b - a, d - a));
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    const V3 corners[4] = {a, b, c, d};
    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i)
    {
        const float weight = i < 2 ? base : tip;
        const uint32_t baked = 0x00ffffffu | (static_cast<uint32_t>(std::lround(255.0f * (1.0f - weight))) << 24);
        mesh.vertices.push_back({{corners[i].x, corners[i].y, corners[i].z}, {n.x, n.y, n.z}, Pack(color), baked, {uvs[i][0], uvs[i][1]}});
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}
} // namespace

Garden MakeGarden()
{
    Garden garden;
    garden.ground = MakeSurface(
        2, 2, [](float u, float v) { return V3{-16.0f + 32.0f * u, 0.0f, -4.0f + 30.0f * v}; }, {0.42f, 0.44f, 0.24f}, true);
    const Color bark{0.40f, 0.31f, 0.22f}, leaf{0.36f, 0.56f, 0.22f}, blade{0.46f, 0.60f, 0.26f}, hemp{0.62f, 0.54f, 0.38f};
    const V3 palms[3] = {{-6.5f, 0.0f, 9.0f}, {-1.5f, 0.0f, 13.0f}, {-9.5f, 0.0f, 15.0f}};
    for (int p = 0; p < 3; ++p)
    {
        const float height = 5.2f + 0.9f * p;
        const V3 top = palms[p] + V3{0.5f - 0.4f * p, height, 0.3f};
        garden.trunks.Append(MakeCylinder(palms[p], top, 0.24f, 0.15f, bark));
        for (int f = 0; f < 11; ++f)
        {
            // Fronds arch out and down from the crown.
            const float angle = 2.0f * kPi * f / 11.0f + 0.4f * p, droop = 0.25f + 0.45f * ((f * 7) % 5) / 4.0f;
            const V3 out{std::cos(angle), 0.0f, std::sin(angle)}, side{-std::sin(angle), 0.0f, std::cos(angle)};
            const V3 mid = top + out * 1.6f + V3{0.0f, 0.55f - droop, 0.0f}, tip = top + out * 3.1f + V3{0.0f, -0.4f - 2.0f * droop, 0.0f};
            AddCard(garden.foliage, top - side * 0.55f, top + side * 0.55f, mid + side * 0.7f, mid - side * 0.7f, leaf, 0.05f, 0.45f);
            AddCard(garden.foliage, mid - side * 0.7f, mid + side * 0.7f, tip + side * 0.35f, tip - side * 0.35f, leaf, 0.45f, 1.0f);
        }
    }
    // The second half of each frond continues the texture: v 0.5..1 (the cards above both map v 0..1).
    for (size_t i = 0; i < garden.foliage.vertices.size(); i += 8)
        for (size_t k = 0; k < 8; ++k)
            garden.foliage.vertices[i + k].uv[1] = 0.5f * garden.foliage.vertices[i + k].uv[1] + (k < 4 ? 0.0f : 0.5f);
    for (int i = 0; i < 90; ++i)
    {
        // Grass: two crossed blades' worth of card per tuft.
        const float x = -14.0f + 28.0f * std::fmod(i * 0.6180339f, 1.0f), z = -2.0f + 14.0f * std::fmod(i * 0.3819660f + 0.2f, 1.0f);
        const float a = 1.9f * i, h = 0.5f + 0.35f * std::fmod(i * 0.754877f, 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            const V3 dir{0.45f * std::cos(a + 1.57f * c), 0.0f, 0.45f * std::sin(a + 1.57f * c)}, base{x, 0.0f, z};
            AddCard(garden.foliage, base - dir, base + dir, base + dir * 1.25f + V3{0.0f, h, 0.0f}, base - dir * 1.25f + V3{0.0f, h, 0.0f}, blade, 0.0f, 0.8f);
        }
    }
    // Two posts with standing rigging between them and stays to the ground.
    const V3 post_a{3.0f, 0.0f, 9.0f}, post_b{11.5f, 0.0f, 12.0f};
    garden.trunks.Append(MakeCylinder(post_a, post_a + V3{0.0f, 7.0f, 0.0f}, 0.14f, 0.10f, bark));
    garden.trunks.Append(MakeCylinder(post_b, post_b + V3{0.0f, 7.0f, 0.0f}, 0.14f, 0.10f, bark));
    const auto rope = [&](V3 from, V3 to, float sag) {
        // Ropes face the camera (which looks along +z): a thin vertical ribbon, in segments so that it can sag and sway.
        V3 previous = from;
        for (int s = 1; s <= 8; ++s)
        {
            const float t = s / 8.0f;
            const V3 next = from + (to - from) * t - V3{0.0f, sag * 4.0f * t * (1.0f - t), 0.0f};
            const float w0 = 4.0f * (t - 0.125f) * (1.125f - t), w1 = 4.0f * t * (1.0f - t);
            const V3 thick{0.0f, 0.035f, 0.0f};
            AddCard(garden.ropes, previous - thick, previous + thick, next + thick, next - thick, hemp, 0.3f * std::max(w0, 0.0f), 0.3f * w1);
            previous = next;
        }
    };
    for (int i = 0; i < 6; ++i)
        rope(post_a + V3{0.0f, 2.0f + 0.9f * i, 0.0f}, post_b + V3{0.0f, 6.8f - 0.9f * i, 0.0f}, 0.25f);
    for (int i = 0; i < 4; ++i)
    {
        rope(post_a + V3{0.0f, 6.9f, 0.0f}, post_a + V3{-3.5f + 0.8f * i, 0.0f, -2.5f}, 0.05f);
        rope(post_b + V3{0.0f, 6.9f, 0.0f}, post_b + V3{3.5f - 0.8f * i, 0.0f, -2.5f}, 0.05f);
    }
    return garden;
}

std::vector<uint8_t> MakeFrondTexture(int size)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (x + 0.5f) / size - 0.5f, v = (y + 0.5f) / size;
            // A rib along v with leaflets slanting towards the tip, narrowing to a point.
            const float reach = 0.48f * (1.0f - 0.75f * v * v);
            const float leaflet = std::fabs(std::fmod((v + 0.35f * std::fabs(u)) * 13.0f, 1.0f) - 0.5f);
            const bool solid = std::fabs(u) < 0.025f || (std::fabs(u) < reach && leaflet < 0.30f);
            const float shade = 0.75f + 0.25f * storm::fx::PeriodicFbm(u + 0.5f, v, 8, 2, 31u) - 0.25f * std::fabs(u);
            uint8_t *texel = &rgba[(static_cast<size_t>(y) * size + x) * 4];
            texel[0] = static_cast<uint8_t>(std::lround(255.0f * std::clamp(0.80f * shade, 0.0f, 1.0f)));
            texel[1] = static_cast<uint8_t>(std::lround(255.0f * std::clamp(shade, 0.0f, 1.0f)));
            texel[2] = static_cast<uint8_t>(std::lround(255.0f * std::clamp(0.62f * shade, 0.0f, 1.0f)));
            texel[3] = solid ? 255 : 0;
        }
    return rgba;
}

std::vector<uint8_t> MakeRopeTexture(int size)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            // The strand runs along v in the middle third of u, with a twist pattern.
            const float v = (x + 0.5f) / size - 0.5f, twist = 0.8f + 0.2f * std::sin((y + 2.0f * x) * 1.6f);
            uint8_t *texel = &rgba[(static_cast<size_t>(y) * size + x) * 4];
            texel[0] = texel[1] = texel[2] = static_cast<uint8_t>(std::lround(255.0f * twist));
            texel[3] = std::fabs(v) < 0.17f ? 255 : 0;
        }
    return rgba;
}

Mesh MakeReliefMesh(Relief kind)
{
    Mesh mesh;
    float tile_u = 1.0f, tile_v = 1.0f;
    switch (kind)
    {
    case Relief::Wall: // 7 x 4.5 m plastered stone wall
        mesh = MakeSurface(
            2, 2, [](float u, float v) { return V3{-8.5f + 7.0f * u, 4.5f * v, 6.0f - 1.2f * u}; }, {0.74f, 0.68f, 0.58f}, true);
        tile_u = 2.4f, tile_v = 1.5f;
        break;
    case Relief::Rock: // boulder
        mesh = MakeSurface(
            48, 24,
            [](float u, float v) {
                const float a = u * 2.0f * kPi, b = (v - 0.5f) * kPi;
                const float r = 1.5f * (1.0f + 0.22f * storm::fx::PeriodicFbm(u, 0.5f * v, 3, 3, 21u));
                return V3{0.6f + r * std::cos(b) * std::cos(a), 0.9f + 0.75f * r * std::sin(b), 4.2f + r * std::cos(b) * std::sin(a)};
            },
            {0.52f, 0.50f, 0.47f});
        tile_u = 3.0f, tile_v = 1.5f;
        break;
    case Relief::Deck: // planks
        mesh = MakeSurface(
            2, 2, [](float u, float v) { return V3{-9.0f + 18.0f * u, 0.0f, -2.0f + 9.0f * v}; }, {0.58f, 0.44f, 0.29f}, true);
        tile_u = 6.0f, tile_v = 3.0f;
        break;
    case Relief::Sail:
        mesh = MakeSurface(
            24, 20,
            [](float u, float v) {
                const float s = u * 2.0f - 1.0f;
                return V3{6.0f + 2.3f * s, 0.6f + 4.6f * v, 5.2f + 0.9f * (1.0f - s * s) * std::sin(v * kPi) + 0.5f * s};
            },
            {0.88f, 0.84f, 0.72f}, true);
        tile_u = 2.0f, tile_v = 4.0f;
        break;
    }
    for (Vertex &v : mesh.vertices)
    {
        v.uv[0] *= tile_u;
        v.uv[1] *= tile_v;
    }
    return mesh;
}

std::vector<uint8_t> MakeHeightMap(Relief kind, int size)
{
    std::vector<uint8_t> height(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
            float h = 0.5f;
            const float grain = storm::fx::PeriodicFbm(u, v, 16, 3, 7u + static_cast<uint32_t>(kind));
            switch (kind)
            {
            case Relief::Wall:
            {
                // Coursed stone: four rows, every other row shifted by half a block, mortar joints recessed.
                const float row = v * 4.0f, column = u * 3.0f + (static_cast<int>(row) % 2 ? 0.5f : 0.0f);
                const float jr = std::fabs(row - std::floor(row) - 0.5f), jc = std::fabs(column - std::floor(column) - 0.5f);
                const float joint = std::min(1.0f, std::min(0.5f - jr, 0.5f - jc) * 14.0f);
                h = 0.25f + 0.5f * joint + 0.12f * grain + 0.08f * storm::fx::PeriodicFbm(u, v, 4, 2, 3u);
                break;
            }
            case Relief::Rock:
            {
                const float ridges = 1.0f - std::fabs(storm::fx::PeriodicFbm(u, v, 4, 4, 11u));
                h = 0.15f + 0.6f * ridges * ridges + 0.15f * grain;
                break;
            }
            case Relief::Deck:
            {
                // Eight planks across v with narrow seams, wood grain along u.
                const float plank = v * 8.0f, seam = std::fabs(plank - std::floor(plank) - 0.5f);
                h = 0.3f + 0.4f * std::min(1.0f, (0.5f - seam) * 12.0f) + 0.10f * storm::fx::PeriodicFbm(u * 0.25f, v, 32, 2, 5u) +
                    0.05f * grain;
                break;
            }
            case Relief::Sail:
                // Canvas weave with seams between the cloths.
                h = 0.5f + 0.10f * std::sin(u * 2.0f * kPi * 48.0f) * std::sin(v * 2.0f * kPi * 48.0f) +
                    0.22f * std::pow(std::fabs(std::sin(u * kPi * 4.0f)), 24.0f) + 0.05f * grain;
                break;
            }
            height[static_cast<size_t>(y) * size + x] = static_cast<uint8_t>(std::lround(std::clamp(h, 0.0f, 1.0f) * 255.0f));
        }
    return height;
}

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

float SeaHeight(float x, float z, float time, float *normal)
{
    // Travelling swells of a fresh breeze: wave vector (rad/m), amplitude (m); deep-water speed w = sqrt(g k).
    constexpr float kWaves[5][3] = {{0.080f, 0.020f, 0.50f}, {0.050f, -0.110f, 0.26f}, {0.210f, 0.090f, 0.14f},
                                    {-0.160f, 0.270f, 0.09f}, {0.450f, -0.380f, 0.04f}};
    float height = 1.2f, dx = 0.0f, dz = 0.0f;
    for (int i = 0; i < 5; ++i)
    {
        const float kx = kWaves[i][0], kz = kWaves[i][1], amp = kWaves[i][2];
        const float phase = kx * x + kz * z - std::sqrt(9.81f * std::sqrt(kx * kx + kz * kz)) * time + 1.3f * i;
        height += amp * std::sin(phase);
        dx += amp * kx * std::cos(phase);
        dz += amp * kz * std::cos(phase);
    }
    if (normal)
    {
        const V3 n = Normalize({-dx, 1.0f, -dz});
        normal[0] = n.x;
        normal[1] = n.y;
        normal[2] = n.z;
    }
    return height;
}

Mesh MakeSea(std::array<float, 2> centre, float time)
{
    // Square grid around `centre`, about 0.5 m apart under the camera and widening cubically to 3 km at the edge.
    constexpr int kCells = 256;
    const auto offset = [](int i) {
        const float s = 2.0f * static_cast<float>(i) / kCells - 1.0f;
        return 64.0f * s + 2950.0f * s * s * s;
    };
    Mesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(kCells + 1) * (kCells + 1));
    for (int z = 0; z <= kCells; ++z)
        for (int x = 0; x <= kCells; ++x)
        {
            Vertex v{};
            v.pos[0] = centre[0] + offset(x);
            v.pos[2] = centre[1] + offset(z);
            v.pos[1] = SeaHeight(v.pos[0], v.pos[2], time, v.normal);
            v.abgr = 0xffffffffu;
            mesh.vertices.push_back(v);
        }
    for (int z = 0; z < kCells; ++z)
        for (int x = 0; x < kCells; ++x)
        {
            const auto at = [](int px, int pz) { return static_cast<uint32_t>(pz * (kCells + 1) + px); };
            mesh.indices.insert(mesh.indices.end(), {at(x, z), at(x, z + 1), at(x + 1, z), at(x + 1, z), at(x, z + 1), at(x + 1, z + 1)});
        }
    return mesh;
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
