#include "storm/fx/town_light.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <thread>

namespace storm::fx
{

namespace
{

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

Vec3 Normalize(Vec3 v)
{
    const float l = std::sqrt(Dot(v, v));
    if (l > 1e-12f)
        for (float &x : v)
            x /= l;
    return v;
}

// Two unit vectors perpendicular to `n` and to each other.
void Basis(const Vec3 &n, Vec3 &t, Vec3 &b)
{
    t = Normalize(Cross(std::fabs(n[1]) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f}, n));
    b = Cross(n, t);
}

constexpr float kSurfaceOffset = 0.05f; // metres along the normal: the ray starts off its own surface

} // namespace

LightmapTexels RasterizeLightmap(std::span<const Vec3> positions, std::span<const std::array<float, 2>> uvs,
                                 std::span<const uint32_t> indices, std::span<const uint32_t> triangle_lightmap,
                                 uint32_t lightmap, int width, int height, int gutter)
{
    LightmapTexels out;
    out.width = width;
    out.height = height;
    if (width <= 0 || height <= 0)
        return out;
    std::vector<int32_t> slot(static_cast<size_t>(width) * height, -1);
    for (size_t t = 0; t + 2 < indices.size() && t / 3 < triangle_lightmap.size(); t += 3)
    {
        if (triangle_lightmap[t / 3] != lightmap)
            continue;
        const uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size() || i0 >= uvs.size() ||
            i1 >= uvs.size() || i2 >= uvs.size())
            continue;
        const Vec3 &a = positions[i0], &b = positions[i1], &c = positions[i2];
        Vec3 n = Cross(Sub(b, a), Sub(c, a));
        if (Dot(n, n) < 1e-12f)
            continue;
        n = Normalize(n);
        // The lit side: up for the ground; for walls the winding (both sides of a wall are separate triangles).
        if (std::fabs(n[1]) > 0.5f && n[1] < 0.0f)
            n = {-n[0], -n[1], -n[2]};
        float px[3], py[3];
        const std::array<float, 2> *t3[3] = {&uvs[i0], &uvs[i1], &uvs[i2]};
        for (int k = 0; k < 3; ++k)
        {
            const float u = (*t3[k])[0] - std::floor((*t3[0])[0]), v = (*t3[k])[1] - std::floor((*t3[0])[1]);
            px[k] = u * static_cast<float>(width) - 0.5f;
            py[k] = v * static_cast<float>(height) - 0.5f;
        }
        const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
        if (std::fabs(area) < 1e-9f)
            continue;
        const int x0 = static_cast<int>(std::floor(std::min({px[0], px[1], px[2]}))) - 1;
        const int x1 = static_cast<int>(std::ceil(std::max({px[0], px[1], px[2]}))) + 1;
        const int y0 = static_cast<int>(std::floor(std::min({py[0], py[1], py[2]}))) - 1;
        const int y1 = static_cast<int>(std::ceil(std::max({py[0], py[1], py[2]}))) + 1;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const int wx = ((x % width) + width) % width, wy = ((y % height) + height) % height;
                const size_t index = static_cast<size_t>(wy) * width + wx;
                if (slot[index] >= 0)
                    continue;
                const float fx = static_cast<float>(x), fy = static_cast<float>(y);
                const float w1 = ((fx - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (fy - py[0])) / area;
                const float w2 = ((px[1] - px[0]) * (fy - py[0]) - (fx - px[0]) * (py[1] - py[0])) / area;
                const float w0 = 1.0f - w1 - w2;
                const float e = -0.02f; // a hair outside the edges: no seams between neighbouring triangles
                if (w0 < e || w1 < e || w2 < e)
                    continue;
                slot[index] = static_cast<int32_t>(out.texel.size());
                out.texel.push_back(static_cast<uint32_t>(index));
                out.position.push_back({w0 * a[0] + w1 * b[0] + w2 * c[0], w0 * a[1] + w1 * b[1] + w2 * c[1],
                                        w0 * a[2] + w1 * b[2] + w2 * c[2]});
                out.normal.push_back(n);
            }
    }
    // The gutter: rings of texels around the covered ones copy their nearest covered neighbour.
    std::vector<int32_t> source(slot.size(), -1);
    std::deque<std::pair<uint32_t, int>> frontier;
    for (size_t i = 0; i < out.texel.size(); ++i)
    {
        source[out.texel[i]] = static_cast<int32_t>(out.texel[i]);
        frontier.push_back({out.texel[i], 0});
    }
    while (!frontier.empty())
    {
        const auto [index, ring] = frontier.front();
        frontier.pop_front();
        if (ring >= gutter)
            continue;
        const int x = static_cast<int>(index % width), y = static_cast<int>(index / width);
        for (const auto &d : {std::array<int, 2>{1, 0}, std::array<int, 2>{-1, 0}, std::array<int, 2>{0, 1}, std::array<int, 2>{0, -1}})
        {
            const int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                continue;
            const uint32_t n = static_cast<uint32_t>(ny) * width + nx;
            if (source[n] >= 0)
                continue;
            source[n] = source[index];
            out.gutter.push_back(n);
            out.gutter.push_back(static_cast<uint32_t>(source[index]));
            frontier.push_back({n, ring + 1});
        }
    }
    return out;
}

std::vector<uint8_t> SkyVisibility(const Occluders &occluders, const LightmapTexels &texels, float reach, int rays,
                                   unsigned threads)
{
    std::vector<uint8_t> out(texels.texel.size(), 255);
    ParallelRanges(texels.texel.size(), threads, [&](size_t first, size_t last) {
        Occluders::Scratch scratch;
        for (size_t i = first; i < last; ++i)
        {
            const Vec3 &n = texels.normal[i];
            Vec3 t, b;
            Basis(n, t, b);
            const Vec3 o{texels.position[i][0] + n[0] * kSurfaceOffset, texels.position[i][1] + n[1] * kSurfaceOffset,
                         texels.position[i][2] + n[2] * kSurfaceOffset};
            int open = 0;
            const float rotation = static_cast<float>((i * 2654435761u) % 1000) * 0.0062831853f; // per texel: no banding
            for (int r = 0; r < rays; ++r)
            {
                // Cosine-weighted directions over the hemisphere (a spiral).
                const float u = (static_cast<float>(r) + 0.5f) / static_cast<float>(rays);
                const float radius = std::sqrt(u), phi = 2.3999632f * static_cast<float>(r) + rotation;
                const float up = std::sqrt(std::max(0.0f, 1.0f - u));
                const Vec3 d{radius * std::cos(phi) * t[0] + radius * std::sin(phi) * b[0] + up * n[0],
                             radius * std::cos(phi) * t[1] + radius * std::sin(phi) * b[1] + up * n[1],
                             radius * std::cos(phi) * t[2] + radius * std::sin(phi) * b[2] + up * n[2]};
                open += occluders.Occluded(o, d, reach, scratch) ? 0 : 1;
            }
            out[i] = static_cast<uint8_t>(std::lround(255.0f * static_cast<float>(open) / static_cast<float>(rays)));
        }
    });
    return out;
}

std::vector<uint8_t> SunVisibility(const Occluders &occluders, const LightmapTexels &texels, const Vec3 &sun,
                                   float texel_metres, int samples, unsigned threads)
{
    std::vector<uint8_t> out(texels.texel.size(), 0);
    Vec3 st, sb;
    Basis(sun, st, sb);
    ParallelRanges(texels.texel.size(), threads, [&](size_t first, size_t last) {
        Occluders::Scratch scratch;
        for (size_t i = first; i < last; ++i)
        {
            const Vec3 &n = texels.normal[i];
            if (Dot(n, sun) <= 0.02f)
                continue; // facing away: its own shadow
            Vec3 t, b;
            Basis(n, t, b);
            int lit = 0;
            for (int s = 0; s < samples; ++s)
            {
                // Over the texel (soft edges at its size) and the sun's disk (half a degree).
                const float a = 6.2831853f * (static_cast<float>(s) + 0.5f) / static_cast<float>(samples);
                const float jx = 0.35f * texel_metres * std::cos(a), jy = 0.35f * texel_metres * std::sin(a);
                const float dx = 0.0045f * std::cos(a + 1.3f), dy = 0.0045f * std::sin(a + 1.3f);
                const Vec3 o{texels.position[i][0] + n[0] * kSurfaceOffset + t[0] * jx + b[0] * jy,
                             texels.position[i][1] + n[1] * kSurfaceOffset + t[1] * jx + b[1] * jy,
                             texels.position[i][2] + n[2] * kSurfaceOffset + t[2] * jx + b[2] * jy};
                const Vec3 d = Normalize({sun[0] + st[0] * dx + sb[0] * dy, sun[1] + st[1] * dx + sb[1] * dy,
                                          sun[2] + st[2] * dx + sb[2] * dy});
                lit += occluders.Occluded(o, d, 300.0f, scratch) ? 0 : 1;
            }
            // The sun's direct light on the texel: the visible share times the cosine (a low sun lights the ground
            // less than a high one, the walls facing it more).
            out[i] = static_cast<uint8_t>(std::lround(255.0f * Dot(n, sun) * static_cast<float>(lit) / static_cast<float>(samples)));
        }
    });
    return out;
}

TownLightTone CalibrateTownLight(const LightmapTexels &texels, std::span<const uint8_t> rgba, std::span<const uint8_t> sun,
                                 std::span<const uint8_t> sky)
{
    TownLightTone tone;
    const size_t n = texels.texel.size();
    if (n < 100 || sun.size() != n || sky.size() != n ||
        rgba.size() < static_cast<size_t>(texels.width) * texels.height * 4)
        return tone;
    // Luminance = a + b sun + c sky, least squares.
    double m[3][4] = {};
    std::vector<double> lum(n);
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const uint8_t *p = &rgba[static_cast<size_t>(texels.texel[i]) * 4];
        lum[i] = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255.0;
        mean += lum[i];
        const double x[3] = {1.0, sun[i] / 255.0, sky[i] / 255.0};
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
                m[r][c] += x[r] * x[c];
            m[r][3] += x[r] * lum[i];
        }
    }
    mean /= static_cast<double>(n);
    for (int i = 0; i < 3; ++i)
    {
        int p = i;
        for (int k = i + 1; k < 3; ++k)
            if (std::fabs(m[k][i]) > std::fabs(m[p][i]))
                p = k;
        for (int j = 0; j < 4; ++j)
            std::swap(m[i][j], m[p][j]);
        if (std::fabs(m[i][i]) < 1e-12)
            return tone;
        for (int k = 0; k < 3; ++k)
            if (k != i)
            {
                const double f = m[k][i] / m[i][i];
                for (int j = 0; j < 4; ++j)
                    m[k][j] -= f * m[i][j];
            }
    }
    tone.a = static_cast<float>(m[0][3] / m[0][0]);
    tone.b = static_cast<float>(m[1][3] / m[1][1]);
    tone.c = static_cast<float>(m[2][3] / m[2][2]);
    double ss = 0.0, sr = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double model = tone.a + tone.b * sun[i] / 255.0 + tone.c * sky[i] / 255.0;
        ss += (lum[i] - mean) * (lum[i] - mean);
        sr += (lum[i] - model) * (lum[i] - model);
    }
    tone.r2 = ss > 0.0 ? static_cast<float>(1.0 - sr / ss) : 0.0f;
    // The sun's part from the game's own shadow edges: the least squares underrate it (the paint's variation and
    // the few texels where our shadows miss the game's dilute it), so the relit lightmap would keep most of the game's
    // shadows and add ours faintly. A shadowed texel next to a sunlit one, both under open sky, has the same paint
    // most of the time: their ratio is the game's shadow depth, and b follows from it with the fitted a and c.
    {
        const int w = texels.width, h = texels.height;
        std::vector<int32_t> index(static_cast<size_t>(w) * h, -1);
        for (size_t i = 0; i < n; ++i)
            index[texels.texel[i]] = static_cast<int32_t>(i);
        std::vector<float> estimates;
        // Lit and shadowed relative to the brightest sun on this lightmap (the cosine: a 45° sun gives the open
        // ground 0.71).
        const int top = *std::max_element(sun.begin(), sun.end());
        const int lit = top * 4 / 5, shade = top / 12;
        constexpr int kOffsets[8][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}, {2, 2}, {-2, 2}, {2, -2}, {-2, -2}};
        for (size_t i = 0; i < n; ++i)
        {
            if (sun[i] > shade || sky[i] < 150 || lum[i] <= 0.0)
                continue;
            const int x = static_cast<int>(texels.texel[i] % w), y = static_cast<int>(texels.texel[i] / w);
            for (const auto &o : kOffsets)
            {
                const int nx = x + o[0], ny = y + o[1];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const int32_t j = index[static_cast<size_t>(ny) * w + nx];
                if (j < 0 || sun[j] < lit || sky[j] < 150 || lum[j] <= 0.0)
                    continue;
                const double r = lum[i] / lum[j];
                const double vi = sun[i] / 255.0, vj = sun[j] / 255.0, ki = sky[i] / 255.0, kj = sky[j] / 255.0;
                const double den = r * vj - vi;
                if (r < 0.05 || r > 0.95 || den < 0.05)
                    continue;
                estimates.push_back(static_cast<float>((tone.a + tone.c * ki - r * (tone.a + tone.c * kj)) / den));
            }
        }
        tone.edges = static_cast<uint32_t>(estimates.size());
        if (estimates.size() >= std::max<size_t>(50, n / 1000))
        {
            std::nth_element(estimates.begin(), estimates.begin() + estimates.size() / 2, estimates.end());
            tone.b = std::max(tone.b, estimates[estimates.size() / 2]);
        }
    }
    // The game's sky per bin of ours (TownLightTone::game_sky).
    if (tone.c > 0.02f)
    {
        double sum[TownLightTone::kSkyBins] = {};
        size_t count[TownLightTone::kSkyBins] = {};
        for (size_t i = 0; i < n; ++i)
        {
            const int bin = std::min(TownLightTone::kSkyBins - 1, sky[i] * TownLightTone::kSkyBins / 256);
            sum[bin] += (lum[i] - tone.a - tone.b * sun[i] / 255.0) / tone.c;
            ++count[bin];
        }
        for (int bin = 0; bin < TownLightTone::kSkyBins; ++bin)
            if (count[bin] >= 200)
                tone.game_sky[bin] = std::clamp(static_cast<float>(sum[bin] / static_cast<double>(count[bin])),
                                                tone.game_sky[bin], 1.0f);
    }
    // The sun must brighten and the lighting stay above zero everywhere (a closed sky, in shadow): a + min(c, 0) > 0.
    tone.valid = tone.b > 0.01f && tone.a + std::min(tone.c, 0.0f) > 0.01f && tone.r2 > 0.05f;
    return tone;
}

float TownLightTone::GameSky(float k) const
{
    const float x = std::clamp(k * kSkyBins - 0.5f, 0.0f, static_cast<float>(kSkyBins - 1));
    const int i = std::min(static_cast<int>(x), kSkyBins - 2);
    return std::max(k, game_sky[i] + (game_sky[i + 1] - game_sky[i]) * (x - static_cast<float>(i)));
}

TownWallTone CalibrateTownWalls(std::span<const Vec3> positions, std::span<const uint32_t> colors,
                                std::span<const uint32_t> indices, std::span<const uint8_t> skip,
                                const VisibilityVolume &volume, const TownLightTone &tone, const Vec3 &bake,
                                const Occluders &occluders, unsigned threads)
{
    TownWallTone wall;
    if (colors.size() != positions.size() || skip.size() != indices.size() / 3 || volume.Empty() || tone.c <= 0.02f)
        return wall;
    // Vertex normals of the walls' triangles (area weighted), and which vertices they use.
    std::vector<Vec3> normal(positions.size(), Vec3{0.0f, 0.0f, 0.0f});
    std::vector<uint8_t> used(positions.size(), 0);
    for (size_t t = 0; t < skip.size(); ++t)
    {
        if (skip[t])
            continue;
        const uint32_t i[3] = {indices[3 * t], indices[3 * t + 1], indices[3 * t + 2]};
        const Vec3 e1{positions[i[1]][0] - positions[i[0]][0], positions[i[1]][1] - positions[i[0]][1], positions[i[1]][2] - positions[i[0]][2]};
        const Vec3 e2{positions[i[2]][0] - positions[i[0]][0], positions[i[2]][1] - positions[i[0]][1], positions[i[2]][2] - positions[i[0]][2]};
        const Vec3 n{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (const uint32_t v : i)
        {
            for (int k = 0; k < 3; ++k)
                normal[v][k] += n[k];
            used[v] = 1;
        }
    }
    std::vector<float> lum(positions.size(), -1.0f), sky(positions.size(), 0.0f);
    std::vector<uint8_t> shaded(positions.size(), 0);
    ParallelRanges(positions.size(), threads, [&](size_t first, size_t last) {
        Occluders::Scratch scratch;
        for (size_t v = first; v < last; ++v)
        {
            if (!used[v])
                continue;
            const float l = std::sqrt(Dot(normal[v], normal[v]));
            if (l < 1e-8f)
                continue;
            const Vec3 n{normal[v][0] / l, normal[v][1] / l, normal[v][2] / l};
            const uint32_t c = colors[v];
            lum[v] = (0.299f * ((c >> 16) & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * (c & 255)) / 255.0f;
            const float cell = volume.cell;
            sky[v] = volume.Sample({positions[v][0] + n[0] * cell, positions[v][1] + n[1] * cell, positions[v][2] + n[2] * cell})[0];
            // In the shade of the bake's sun: facing away, or its ray closed.
            const Vec3 o{positions[v][0] + n[0] * kSurfaceOffset, positions[v][1] + n[1] * kSurfaceOffset,
                         positions[v][2] + n[2] * kSurfaceOffset};
            shaded[v] = Dot(n, bake) <= 0.05f || occluders.Occluded(o, bake, 300.0f, scratch) ? 1 : 0;
        }
    });
    double open_sum = 0.0, open_sky = 0.0, bin_sum[TownWallTone::kSkyBins] = {};
    size_t open_count = 0;
    for (size_t v = 0; v < positions.size(); ++v)
    {
        if (lum[v] < 0.0f)
            continue;
        ++wall.vertices;
        if (!shaded[v])
            continue;
        ++wall.shaded;
        const int bin = std::min(TownWallTone::kSkyBins - 1, static_cast<int>(sky[v] * TownWallTone::kSkyBins));
        bin_sum[bin] += lum[v];
        ++wall.count[bin];
        if (sky[v] >= 0.75f)
        {
            open_sum += lum[v];
            open_sky += sky[v];
            ++open_count;
        }
    }
    if (open_count < 100 || open_sum <= 0.0)
        return wall;
    const double open_lum = open_sum / static_cast<double>(open_count), k_open = open_sky / static_cast<double>(open_count);
    for (int bin = 0; bin < TownWallTone::kSkyBins; ++bin)
    {
        if (wall.count[bin] < 50)
            continue;
        const double r = bin_sum[bin] / static_cast<double>(wall.count[bin]) / open_lum;
        wall.relative[bin] = static_cast<float>(r);
        wall.game_sky[bin] = std::clamp(static_cast<float>((r * (tone.a + tone.c * k_open) - tone.a) / tone.c),
                                        wall.game_sky[bin], 1.0f);
    }
    wall.valid = true;
    return wall;
}

std::vector<uint8_t> TexelBounce(const LightmapTexels &texels, const BounceField &field, float offset, unsigned threads)
{
    std::vector<uint8_t> out(texels.texel.size() * 3, 0);
    if (field.Empty())
        return out;
    ParallelRanges(texels.texel.size(), threads, [&](size_t first, size_t last) {
        for (size_t i = first; i < last; ++i)
        {
            const Vec3 &p = texels.position[i], &n = texels.normal[i];
            const Rgb e = field.Irradiance({p[0] + n[0] * offset, p[1] + n[1] * offset, p[2] + n[2] * offset}, n);
            for (int c = 0; c < 3; ++c)
                out[i * 3 + c] = static_cast<uint8_t>(std::lround(255.0f * std::clamp(e[c], 0.0f, 1.0f)));
        }
    });
    return out;
}

void ComposeTownLight(const LightmapTexels &texels, const TownLightTone &tone, std::span<const uint8_t> game,
                      std::span<const uint8_t> baked, std::span<const uint8_t> sun_a, std::span<const uint8_t> sun_b,
                      float t, std::span<const uint8_t> sky, std::span<uint8_t> rgba, const TownBounce &bounce)
{
    const size_t n = texels.texel.size();
    const size_t bytes = static_cast<size_t>(texels.width) * texels.height * 4;
    if (baked.size() != n || sun_a.size() != n || sun_b.size() != n || sky.size() != n || rgba.size() < bytes ||
        game.size() < bytes)
        return;
    const bool bounced = bounce.a.size() == n * 3 && bounce.b.size() == n * 3 && bounce.baked.size() == n * 3;
    for (size_t i = 0; i < n; ++i)
    {
        const float k = sky[i] / 255.0f;
        const float now = tone.a + tone.b * ((1.0f - t) * sun_a[i] + t * sun_b[i]) / 255.0f + tone.c * k;
        const float then = tone.a + tone.b * baked[i] / 255.0f + tone.c * tone.GameSky(k);
        const size_t at = static_cast<size_t>(texels.texel[i]) * 4;
        for (int ch = 0; ch < 3; ++ch)
        {
            float lit = now, was = then;
            if (bounced)
            {
                lit += tone.b * ((1.0f - bounce.t) * bounce.a[i * 3 + ch] + bounce.t * bounce.b[i * 3 + ch]) / 255.0f;
                was += tone.b * bounce.baked[i * 3 + ch] / 255.0f;
            }
            // The paint under the game's light, lit by ours (a texel the game had in a shadow we do not see, or the
            // other way round, at most 3x brighter / darker).
            const float ratio = std::clamp(lit / std::max(was, 0.02f), 0.33f, 3.0f);
            rgba[at + ch] = static_cast<uint8_t>(std::lround(std::clamp(game[at + ch] * ratio, 0.0f, 255.0f)));
        }
        rgba[at + 3] = game[at + 3];
    }
    for (size_t g = 0; g + 1 < texels.gutter.size(); g += 2)
        std::copy_n(&rgba[static_cast<size_t>(texels.gutter[g + 1]) * 4], 4, &rgba[static_cast<size_t>(texels.gutter[g]) * 4]);
}

} // namespace storm::fx
