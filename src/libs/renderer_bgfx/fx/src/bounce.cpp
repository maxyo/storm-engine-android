#include "storm/fx/bounce.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::fx
{

namespace
{

constexpr uint32_t kMagic = 0x4e434f42u; // "BOCN"
constexpr float kSurfaceOffset = 0.05f;  // metres off a surfel for its shadow rays
constexpr int kMaxSplit = 64;            // a triangle's surfels per edge at most

Vec3 Sub(const Vec3 &a, const Vec3 &b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 Cross(const Vec3 &a, const Vec3 &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float Dot(const Vec3 &a, const Vec3 &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Length(const Vec3 &a)
{
    return std::sqrt(Dot(a, a));
}

float Luminance(float r, float g, float b)
{
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

uint64_t Hash(uint64_t h, const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// A unit vector perpendicular to `n`.
Vec3 Tangent(const Vec3 &n)
{
    const Vec3 a = std::fabs(n[1]) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 t = Cross(n, a);
    const float l = Length(t);
    return {t[0] / l, t[1] / l, t[2] / l};
}

// Surfels per edge of a triangle with corners a, b, c.
int Split(const Vec3 &a, const Vec3 &b, const Vec3 &c, float surfel)
{
    const float edge = std::max({Length(Sub(b, a)), Length(Sub(c, a)), Length(Sub(c, b))});
    return std::clamp(static_cast<int>(std::ceil(edge / surfel)), 1, kMaxSplit);
}

// The surfel of barycentric (u, v) (of the second and third corners) on a triangle split `n` times per edge: the
// cells (i, j) of the (u, v) grid with i + j < n, row by row.
uint32_t SurfelOf(float u, float v, int n)
{
    const int i = std::clamp(static_cast<int>(u * static_cast<float>(n)), 0, n - 1);
    const int j = std::clamp(static_cast<int>(v * static_cast<float>(n)), 0, n - 1 - i);
    return static_cast<uint32_t>(i * n - i * (i - 1) / 2 + j);
}

// The 12 moments of `field` at lattice coordinates f (clamped), trilinear.
void Interpolate(const BounceField &field, const float f[3], float out[12])
{
    int i0[3], i1[3];
    float w[3];
    for (int k = 0; k < 3; ++k)
    {
        const float c = std::clamp(f[k], 0.0f, static_cast<float>(field.points[k] - 1));
        i0[k] = static_cast<int>(std::floor(c));
        i1[k] = std::min(i0[k] + 1, field.points[k] - 1);
        w[k] = c - static_cast<float>(i0[k]);
    }
    std::fill(out, out + 12, 0.0f);
    for (int corner = 0; corner < 8; ++corner)
    {
        const int x = corner & 1 ? i1[0] : i0[0], y = corner & 2 ? i1[1] : i0[1], z = corner & 4 ? i1[2] : i0[2];
        const float weight = (corner & 1 ? w[0] : 1.0f - w[0]) * (corner & 2 ? w[1] : 1.0f - w[1]) *
                             (corner & 4 ? w[2] : 1.0f - w[2]);
        if (weight <= 0.0f)
            continue;
        const float *m = &field.moments[((static_cast<size_t>(z) * field.points[1] + y) * field.points[0] + x) * 12];
        for (int k = 0; k < 12; ++k)
            out[k] += weight * m[k];
    }
}

} // namespace

Rgb BounceTexture::At(float u, float v) const
{
    if (srgb.empty())
        return {0.5f, 0.5f, 0.5f};
    const float x = (u - std::floor(u)) * static_cast<float>(width) - 0.5f;
    const float y = (v - std::floor(v)) * static_cast<float>(height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    Rgb out{0.0f, 0.0f, 0.0f};
    for (int c = 0; c < 4; ++c)
    {
        const int xi = ((x0 + (c & 1)) % width + width) % width, yi = ((y0 + (c >> 1)) % height + height) % height;
        const float w = (c & 1 ? fx : 1.0f - fx) * (c & 2 ? fy : 1.0f - fy);
        const uint8_t *p = &srgb[(static_cast<size_t>(yi) * width + xi) * 3];
        for (int k = 0; k < 3; ++k)
            out[k] += w * static_cast<float>(p[k]) * (1.0f / 255.0f);
    }
    return out;
}

Vec3 BounceScene::PointPosition(size_t index) const
{
    const int x = static_cast<int>(index % points[0]), y = static_cast<int>((index / points[0]) % points[1]),
              z = static_cast<int>(index / (static_cast<size_t>(points[0]) * points[1]));
    return {grid.origin[0] + (static_cast<float>(x * stride) + 0.5f) * grid.cell,
            grid.origin[1] + (static_cast<float>(y * stride) + 0.5f) * grid.cell,
            grid.origin[2] + (static_cast<float>(z * stride) + 0.5f) * grid.cell};
}

BounceScene BuildBounceScene(const Occluders &occluders, const BounceGeometry &geometry, const BounceGrid &grid,
                             const BounceParams &params)
{
    BounceScene scene;
    const size_t triangles = geometry.indices.size() / 3;
    if (triangles == 0 || occluders.Empty() || occluders.Triangles() != triangles || grid.cell <= 0.0f ||
        grid.size[0] <= 0 || grid.size[1] <= 0 || grid.size[2] <= 0)
        return scene;
    scene.grid = grid;
    scene.rays = std::max(8, params.rays);
    const float surfel = std::max(params.surfel, 0.1f);
    // The surfels.
    std::vector<uint32_t> first(triangles + 1, 0);
    std::vector<uint8_t> split(triangles);
    for (size_t t = 0; t < triangles; ++t)
    {
        const Vec3 &a = geometry.positions[geometry.indices[3 * t]], &b = geometry.positions[geometry.indices[3 * t + 1]],
                   &c = geometry.positions[geometry.indices[3 * t + 2]];
        const int n = Split(a, b, c, surfel);
        split[t] = static_cast<uint8_t>(n);
        first[t + 1] = first[t] + static_cast<uint32_t>(n * (n + 1) / 2);
    }
    const size_t surfels = first.back();
    scene.position.resize(surfels);
    scene.normal.resize(surfels);
    scene.albedo.resize(surfels);
    const bool textured = geometry.uvs.size() == geometry.positions.size() && geometry.triangle_texture.size() == triangles;
    for (size_t t = 0; t < triangles; ++t)
    {
        const uint32_t ia = geometry.indices[3 * t], ib = geometry.indices[3 * t + 1], ic = geometry.indices[3 * t + 2];
        const Vec3 &a = geometry.positions[ia];
        const Vec3 e1 = Sub(geometry.positions[ib], a), e2 = Sub(geometry.positions[ic], a);
        Vec3 normal = Cross(e1, e2);
        const float area = Length(normal);
        normal = area > 1e-12f ? Vec3{normal[0] / area, normal[1] / area, normal[2] / area} : Vec3{0.0f, 1.0f, 0.0f};
        const uint32_t texture = textured ? geometry.triangle_texture[t] : kBounceNoTexture;
        const BounceTexture *image = texture < geometry.textures.size() && geometry.textures[texture] &&
                                             !geometry.textures[texture]->Empty()
                                         ? geometry.textures[texture]
                                         : nullptr;
        if (!image)
            ++scene.untextured;
        const int n = split[t];
        uint32_t s = first[t];
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n - i; ++j, ++s)
            {
                // The centroid of the cell's lower triangle (inside the triangle for every cell).
                const float u = (static_cast<float>(i) + 1.0f / 3.0f) / static_cast<float>(n);
                const float v = (static_cast<float>(j) + 1.0f / 3.0f) / static_cast<float>(n);
                scene.position[s] = {a[0] + u * e1[0] + v * e2[0], a[1] + u * e1[1] + v * e2[1], a[2] + u * e1[2] + v * e2[2]};
                scene.normal[s] = normal;
                if (image)
                {
                    const auto &ta = geometry.uvs[ia], &tb = geometry.uvs[ib], &tc = geometry.uvs[ic];
                    const float w = 1.0f - u - v;
                    scene.albedo[s] = image->At(w * ta[0] + u * tb[0] + v * tc[0], w * ta[1] + u * tb[1] + v * tc[1]);
                }
                else
                    scene.albedo[s] = {0.5f, 0.5f, 0.5f};
            }
    }
    // The lattice: every stride-th cell, the stride grown until the records fit.
    for (scene.stride = 1;; ++scene.stride)
    {
        size_t points = 1;
        for (int k = 0; k < 3; ++k)
        {
            scene.points[k] = (grid.size[k] - 1 + scene.stride - 1) / scene.stride + 1;
            points *= static_cast<size_t>(scene.points[k]);
        }
        if (points * static_cast<size_t>(scene.rays) <= params.max_records)
            break;
    }
    const size_t points = scene.Points();
    scene.hit.assign(points * scene.rays, BounceScene::kMiss);
    scene.inside.assign(points, 0);
    scene.link.assign(points, 0);
    float diagonal = 0.0f;
    for (int k = 0; k < 3; ++k)
        diagonal += (grid.size[k] * grid.cell) * (grid.size[k] * grid.cell);
    const float reach = std::min(params.reach, std::sqrt(diagonal) + grid.cell);
    const int rays = scene.rays;
    ParallelRanges(points, params.threads, [&](size_t begin, size_t end) {
        Occluders::Scratch scratch;
        for (size_t p = begin; p < end; ++p)
        {
            const Vec3 o = scene.PointPosition(p);
            const float rotation = static_cast<float>((p * 2654435761u) % 1000) * 0.0062831853f; // per point: no banding
            int front = 0, back = 0;
            for (int r = 0; r < rays; ++r)
            {
                // A spherical Fibonacci set, turned about y per point.
                const float y = 1.0f - (2.0f * static_cast<float>(r) + 1.0f) / static_cast<float>(rays);
                const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y)), phi = 2.3999632f * static_cast<float>(r) + rotation;
                const Vec3 d{radius * std::cos(phi), y, radius * std::sin(phi)};
                const std::optional<Occluders::Hit> hit = occluders.Closest(o, d, reach, scratch);
                if (!hit)
                    continue;
                const uint32_t t = hit->triangle;
                const uint32_t s = first[t] + SurfelOf(hit->u, hit->v, split[t]);
                const bool facing = Dot(d, scene.normal[s]) < 0.0f;
                (facing ? front : back) += 1;
                scene.hit[p * rays + r] = s * 2u + (facing ? 1u : 0u);
            }
            scene.inside[p] = back > front ? 1 : 0;
            const int at[3] = {static_cast<int>(p % scene.points[0]), static_cast<int>((p / scene.points[0]) % scene.points[1]),
                               static_cast<int>(p / (static_cast<size_t>(scene.points[0]) * scene.points[1]))};
            const float spacing = grid.cell * static_cast<float>(scene.stride);
            for (int k = 0; k < 3; ++k)
            {
                if (at[k] + 1 >= scene.points[k])
                    continue;
                Vec3 d{0.0f, 0.0f, 0.0f};
                d[k] = 1.0f;
                if (!occluders.Occluded(o, d, spacing, scratch))
                    scene.link[p] |= static_cast<uint8_t>(1u << k);
            }
        }
    });
    return scene;
}

uint64_t BounceKey(const BounceGeometry &geometry, const BounceGrid &grid, const BounceParams &params)
{
    uint64_t key = 1469598103934665603ull;
    key = Hash(key, &kBounceBakeVersion, sizeof(kBounceBakeVersion));
    key = Hash(key, geometry.positions.data(), geometry.positions.size_bytes());
    key = Hash(key, geometry.uvs.data(), geometry.uvs.size_bytes());
    key = Hash(key, geometry.indices.data(), geometry.indices.size_bytes());
    key = Hash(key, geometry.triangle_texture.data(), geometry.triangle_texture.size_bytes());
    for (const BounceTexture *t : geometry.textures)
    {
        const int size[2] = {t ? t->width : 0, t ? t->height : 0};
        key = Hash(key, size, sizeof(size));
        if (t)
            key = Hash(key, t->srgb.data(), t->srgb.size());
    }
    key = Hash(key, grid.size.data(), sizeof(grid.size));
    key = Hash(key, grid.origin.data(), sizeof(grid.origin));
    key = Hash(key, &grid.cell, sizeof(grid.cell));
    const float p[3] = {params.surfel, params.reach, static_cast<float>(params.rays)};
    key = Hash(key, p, sizeof(p));
    return Hash(key, &params.max_records, sizeof(params.max_records));
}

std::vector<uint8_t> EncodeBounceScene(const BounceScene &scene, uint64_t key)
{
    const uint32_t surfels = static_cast<uint32_t>(scene.position.size());
    std::vector<uint8_t> out;
    auto put = [&](const void *data, size_t size) {
        out.insert(out.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    };
    put(&kMagic, 4);
    put(&key, 8);
    put(scene.grid.size.data(), 12);
    put(scene.grid.origin.data(), 12);
    put(&scene.grid.cell, 4);
    put(&scene.stride, 4);
    put(scene.points.data(), 12);
    put(&scene.rays, 4);
    put(&scene.untextured, 4);
    put(&surfels, 4);
    put(scene.position.data(), surfels * sizeof(Vec3));
    put(scene.normal.data(), surfels * sizeof(Vec3));
    put(scene.albedo.data(), surfels * sizeof(Rgb));
    put(scene.hit.data(), scene.hit.size() * sizeof(uint32_t));
    put(scene.inside.data(), scene.inside.size());
    put(scene.link.data(), scene.link.size());
    return out;
}

std::optional<BounceScene> DecodeBounceScene(std::span<const uint8_t> bytes, uint64_t key, std::string &error)
{
    size_t at = 0;
    auto take = [&](void *data, size_t size) {
        if (at + size > bytes.size())
            return false;
        std::memcpy(data, bytes.data() + at, size);
        at += size;
        return true;
    };
    BounceScene s;
    uint32_t magic = 0, surfels = 0;
    uint64_t stored = 0;
    if (!take(&magic, 4) || !take(&stored, 8) || magic != kMagic || stored != key)
    {
        error = "another bake";
        return std::nullopt;
    }
    if (!take(s.grid.size.data(), 12) || !take(s.grid.origin.data(), 12) || !take(&s.grid.cell, 4) || !take(&s.stride, 4) ||
        !take(s.points.data(), 12) || !take(&s.rays, 4) || !take(&s.untextured, 4) || !take(&surfels, 4) || s.stride < 1 ||
        s.rays < 1 || s.points[0] < 1 || s.points[1] < 1 || s.points[2] < 1)
    {
        error = "a broken header";
        return std::nullopt;
    }
    const size_t points = s.Points();
    if (bytes.size() - at != surfels * (2 * sizeof(Vec3) + sizeof(Rgb)) + points * s.rays * sizeof(uint32_t) + 2 * points)
    {
        error = "its size does not match its surfels and points";
        return std::nullopt;
    }
    s.position.resize(surfels);
    s.normal.resize(surfels);
    s.albedo.resize(surfels);
    s.hit.resize(points * s.rays);
    s.inside.resize(points);
    s.link.resize(points);
    take(s.position.data(), surfels * sizeof(Vec3));
    take(s.normal.data(), surfels * sizeof(Vec3));
    take(s.albedo.data(), surfels * sizeof(Rgb));
    take(s.hit.data(), s.hit.size() * sizeof(uint32_t));
    take(s.inside.data(), s.inside.size());
    take(s.link.data(), s.link.size());
    for (const uint32_t h : s.hit)
        if (h != BounceScene::kMiss && h / 2 >= surfels)
        {
            error = "a ray hits a surfel it does not have";
            return std::nullopt;
        }
    return s;
}

SurfelLight SunOnSurfels(const Occluders &occluders, const BounceScene &scene, const Vec3 &sun, const Rgb &color,
                         unsigned threads)
{
    const size_t surfels = scene.position.size();
    SurfelLight light(surfels * 2, Rgb{0.0f, 0.0f, 0.0f});
    if (sun[1] <= 0.0f)
        return light; // below the horizon
    ParallelRanges(surfels, threads, [&](size_t begin, size_t end) {
        Occluders::Scratch scratch;
        for (size_t s = begin; s < end; ++s)
        {
            const float d = Dot(scene.normal[s], sun);
            if (std::fabs(d) <= 0.02f)
                continue;
            // The side that faces the sun, two rays across the surfel (soft edges at its size).
            const float side = d > 0.0f ? 1.0f : -1.0f;
            const Vec3 n{scene.normal[s][0] * side, scene.normal[s][1] * side, scene.normal[s][2] * side};
            const Vec3 t = Tangent(n);
            int lit = 0;
            for (int k = 0; k < 2; ++k)
            {
                const float j = (k ? 0.25f : -0.25f) * 0.5f;
                const Vec3 o{scene.position[s][0] + n[0] * kSurfaceOffset + t[0] * j,
                             scene.position[s][1] + n[1] * kSurfaceOffset + t[1] * j,
                             scene.position[s][2] + n[2] * kSurfaceOffset + t[2] * j};
                lit += occluders.Occluded(o, sun, kSunRayReach, scratch) ? 0 : 1;
            }
            const float e = std::fabs(d) * 0.5f * static_cast<float>(lit);
            light[s * 2 + (d > 0.0f ? 1 : 0)] = {color[0] * e, color[1] * e, color[2] * e};
        }
    });
    return light;
}

SurfelLight LampsOnSurfels(const Occluders &occluders, const BounceScene &scene, std::span<const Lamp> lamps,
                           std::span<const float> holder, std::span<const std::vector<uint32_t>> fixture, unsigned threads)
{
    const size_t surfels = scene.position.size();
    SurfelLight light(surfels * 2, Rgb{0.0f, 0.0f, 0.0f});
    if (lamps.empty())
        return light;
    ParallelRanges(surfels, threads, [&](size_t begin, size_t end) {
        Occluders::Scratch scratch;
        for (size_t s = begin; s < end; ++s)
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                const Lamp &lamp = lamps[i];
                const Vec3 l = Sub(lamp.position, scene.position[s]);
                const float reach = Length(l);
                if (reach >= lamp.range || reach < 1e-4f)
                    continue;
                const Vec3 dir{l[0] / reach, l[1] / reach, l[2] / reach};
                const float d = Dot(scene.normal[s], dir);
                if (std::fabs(d) <= 0.02f)
                    continue;
                // ffLampAdd's falloff of a lamp of the place.
                const float falloff = LightFalloffGammaAt(ShapeOf(lamp), reach);
                const float size = i < holder.size() ? holder[i] : 0.0f;
                const float side = d > 0.0f ? 1.0f : -1.0f;
                const Vec3 o{scene.position[s][0] + scene.normal[s][0] * side * kSurfaceOffset,
                             scene.position[s][1] + scene.normal[s][1] * side * kSurfaceOffset,
                             scene.position[s][2] + scene.normal[s][2] * side * kSurfaceOffset};
                // The lamp's own fixture does not shadow it.
                if (occluders.Occluded(o, dir, std::max(0.01f, reach - size), scratch,
                                       i < fixture.size() ? std::span<const uint32_t>(fixture[i]) : std::span<const uint32_t>{}))
                    continue;
                const float e = std::fabs(d) * falloff;
                Rgb &out = light[s * 2 + (d > 0.0f ? 1 : 0)];
                for (int k = 0; k < 3; ++k)
                    out[k] += lamp.color[k] * e;
            }
    });
    return light;
}

Rgb BounceField::Irradiance(const Vec3 &world, const Vec3 &n) const
{
    if (Empty())
        return {0.0f, 0.0f, 0.0f};
    float f[3], m[12];
    for (int k = 0; k < 3; ++k)
        f[k] = (world[k] - origin[k]) / spacing;
    Interpolate(*this, f, m);
    // As ffBounce: the direction from the luminance, the colour from c.
    const float y = Luminance(m[0], m[1], m[2]);
    if (y <= 1e-6f)
        return {0.0f, 0.0f, 0.0f};
    float lobe = 1.0f;
    for (int k = 0; k < 3; ++k)
        lobe += std::clamp(2.0f * Luminance(m[3 + 3 * k], m[4 + 3 * k], m[5 + 3 * k]) / y, -2.0f, 2.0f) * n[k];
    lobe = std::max(0.0f, lobe);
    return {m[0] * lobe, m[1] * lobe, m[2] * lobe};
}

double BounceField::Energy() const
{
    double sum = 0.0;
    for (size_t i = 0; i + 12 <= moments.size(); i += 12)
        sum += Luminance(moments[i], moments[i + 1], moments[i + 2]);
    return sum;
}

BounceField GatherBounce(const BounceScene &scene, const SurfelLight &light, unsigned threads)
{
    BounceField field;
    if (scene.Empty() || light.size() != scene.position.size() * 2)
        return field;
    field.points = scene.points;
    field.spacing = scene.grid.cell * static_cast<float>(scene.stride);
    for (int k = 0; k < 3; ++k)
        field.origin[k] = scene.grid.origin[k] + 0.5f * scene.grid.cell;
    const size_t points = scene.Points();
    const int rays = scene.rays;
    field.moments.assign(points * 12, 0.0f);
    ParallelRanges(points, threads, [&](size_t begin, size_t end) {
        for (size_t p = begin; p < end; ++p)
        {
            if (scene.inside[p])
                continue;
            const float rotation = static_cast<float>((p * 2654435761u) % 1000) * 0.0062831853f; // as the bake
            float *m = &field.moments[p * 12];
            for (int r = 0; r < rays; ++r)
            {
                const uint32_t h = scene.hit[p * rays + r];
                if (h == BounceScene::kMiss)
                    continue;
                const Rgb &e = light[h];
                const Rgb &a = scene.albedo[h / 2];
                const float L[3] = {a[0] * e[0], a[1] * e[1], a[2] * e[2]};
                if (L[0] + L[1] + L[2] <= 0.0f)
                    continue;
                const float y = 1.0f - (2.0f * static_cast<float>(r) + 1.0f) / static_cast<float>(rays);
                const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y)), phi = 2.3999632f * static_cast<float>(r) + rotation;
                const float d[3] = {radius * std::cos(phi), y, radius * std::sin(phi)};
                for (int ch = 0; ch < 3; ++ch)
                {
                    m[ch] += L[ch];
                    for (int k = 0; k < 3; ++k)
                        m[3 + 3 * k + ch] += L[ch] * d[k];
                }
            }
            for (int k = 0; k < 12; ++k)
                m[k] /= static_cast<float>(rays);
        }
    });
    // Points inside the geometry: the mean of their open neighbours (grown a few steps in from the surface).
    const int step[3] = {1, scene.points[0], scene.points[0] * scene.points[1]};
    std::vector<uint8_t> known(points);
    for (size_t p = 0; p < points; ++p)
        known[p] = scene.inside[p] ? 0 : 1;
    for (int pass = 0; pass < 3; ++pass)
    {
        std::vector<uint8_t> now = known;
        for (size_t p = 0; p < points; ++p)
        {
            if (known[p])
                continue;
            const int at[3] = {static_cast<int>(p % scene.points[0]), static_cast<int>((p / scene.points[0]) % scene.points[1]),
                               static_cast<int>(p / (static_cast<size_t>(scene.points[0]) * scene.points[1]))};
            float sum[12] = {};
            int count = 0;
            for (int k = 0; k < 3; ++k)
                for (int s = -1; s <= 1; s += 2)
                {
                    if (at[k] + s < 0 || at[k] + s >= scene.points[k])
                        continue;
                    const size_t o = p + static_cast<size_t>(static_cast<std::ptrdiff_t>(s) * step[k]);
                    if (!known[o] || !(scene.link[s > 0 ? p : o] & (1u << k)))
                        continue;
                    for (int c = 0; c < 12; ++c)
                        sum[c] += field.moments[o * 12 + c];
                    ++count;
                }
            if (count == 0)
                continue;
            for (int c = 0; c < 12; ++c)
                field.moments[p * 12 + c] = sum[c] / static_cast<float>(count);
            now[p] = 1;
        }
        known.swap(now);
    }
    // One [1 2 1] blur along each axis.
    std::vector<float> blurred(field.moments.size());
    for (int k = 0; k < 3; ++k)
    {
        for (size_t p = 0; p < points; ++p)
        {
            const int at = static_cast<int>((p / static_cast<size_t>(step[k])) % scene.points[k]);
            const size_t lo = at > 0 && (scene.link[p - step[k]] & (1u << k)) ? p - step[k] : p;
            const size_t hi = at + 1 < scene.points[k] && (scene.link[p] & (1u << k)) ? p + step[k] : p;
            for (int c = 0; c < 12; ++c)
                blurred[p * 12 + c] =
                    0.25f * field.moments[lo * 12 + c] + 0.5f * field.moments[p * 12 + c] + 0.25f * field.moments[hi * 12 + c];
        }
        field.moments.swap(blurred);
    }
    return field;
}

void EncodeBounceBlocks(const BounceGrid &grid, const BounceField &field, const Rgb &scale, const BounceField *add,
                        std::span<uint8_t> visibility, std::vector<uint8_t> &bounce, unsigned threads)
{
    const size_t cells = static_cast<size_t>(grid.size[0]) * grid.size[1] * grid.size[2];
    bounce.assign(cells * 4, 0);
    if (field.Empty() || visibility.size() != cells * 4)
        return;
    const bool adding = add && !add->Empty() && add->points == field.points;
    const float per_cell = grid.cell / field.spacing; // lattice steps per cell
    ParallelRanges(cells, threads, [&](size_t first, size_t last) {
    for (size_t c = first; c < last; ++c)
    {
        const int at[3] = {static_cast<int>(c % grid.size[0]), static_cast<int>((c / grid.size[0]) % grid.size[1]),
                           static_cast<int>(c / (static_cast<size_t>(grid.size[0]) * grid.size[1]))};
        const float f[3] = {at[0] * per_cell, at[1] * per_cell, at[2] * per_cell};
        float m[12], extra[12];
        Interpolate(field, f, m);
        for (int k = 0; k < 12; ++k)
            m[k] *= scale[k % 3];
        if (adding)
        {
            Interpolate(*add, f, extra);
            for (int k = 0; k < 12; ++k)
                m[k] += extra[k];
        }
        const float y = Luminance(m[0], m[1], m[2]);
        float v[3] = {0.0f, 0.0f, 0.0f};
        if (y > 1e-6f)
            for (int k = 0; k < 3; ++k)
                v[k] = std::clamp(2.0f * Luminance(m[3 + 3 * k], m[4 + 3 * k], m[5 + 3 * k]) / y, -2.0f, 2.0f);
        auto unit = [](float x) { return static_cast<uint8_t>(std::lround(255.0f * std::clamp(x, 0.0f, 1.0f))); };
        for (int ch = 0; ch < 3; ++ch)
            bounce[c * 4 + ch] = unit(std::sqrt(std::max(0.0f, m[ch]) / kBounceRange));
        bounce[c * 4 + 3] = unit(v[1] * 0.25f + 0.5f);
        visibility[c * 4 + 2] = unit(v[0] * 0.25f + 0.5f);
        visibility[c * 4 + 3] = unit(v[2] * 0.25f + 0.5f);
    }
    });
}

} // namespace storm::fx
