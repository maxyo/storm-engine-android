// Far LOD host check (run.py): the levels of synthetic models (verify.hpp says what is checked), the result
// independent of the threads, and a broken layout refused. The game's models: tools/game_checks/far_lod.
#include "verify.hpp"

namespace
{
using namespace far_lod_check;

struct Builder
{
    Model model;
    std::vector<uint8_t> vertices; // one buffer, VERTEX0 (36 bytes)

    uint32_t vertex(float x, float y, float z, float u, float v)
    {
        uint8_t raw[36] = {};
        const float pos[3] = {x, y, z}, normal[3] = {0, 1, 0}, uv[2] = {u, v};
        std::memcpy(raw, pos, 12);
        std::memcpy(raw + 12, normal, 12);
        std::memcpy(raw + 28, uv, 8);
        vertices.insert(vertices.end(), raw, raw + 36);
        return static_cast<uint32_t>(vertices.size() / 36 - 1);
    }
    // A bumpy n×n grid from (x0, z0), `cell` metres per cell, as one object.
    void grid(float x0, float z0, int n, float cell, float bump)
    {
        MeshObject object;
        object.first_vertex = static_cast<uint32_t>(vertices.size() / 36);
        object.first_triangle = static_cast<uint32_t>(model.triangles.size() / 3);
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i)
            {
                const float x = x0 + i * cell, z = z0 + j * cell;
                vertex(x, bump * std::sin(x * 0.05f) * std::cos(z * 0.07f), z, x * 0.1f, z * 0.1f);
            }
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                const uint16_t a = static_cast<uint16_t>(j * (n + 1) + i), b = a + 1, c = a + (n + 1), d = c + 1;
                model.triangles.insert(model.triangles.end(), {a, c, b, b, c, d});
            }
        object.vertex_count = static_cast<uint32_t>(vertices.size() / 36) - object.first_vertex;
        object.triangle_count = static_cast<uint32_t>(model.triangles.size() / 3) - object.first_triangle;
        model.objects.push_back(object);
    }
    // A decal: a quad 2 mm above the grid's flat top at height `y`, after a quad of the base in the same plane.
    void decal_pair(float x, float z, float y)
    {
        MeshObject object;
        object.first_vertex = static_cast<uint32_t>(vertices.size() / 36);
        object.first_triangle = static_cast<uint32_t>(model.triangles.size() / 3);
        for (const float h : {y, y + 0.002f})
        {
            const uint16_t a = static_cast<uint16_t>(vertices.size() / 36 - object.first_vertex);
            vertex(x, h, z, 0, 0), vertex(x + 1, h, z, 1, 0), vertex(x, h, z + 1, 0, 1), vertex(x + 1, h, z + 1, 1, 1);
            model.triangles.insert(model.triangles.end(), {a, uint16_t(a + 2), uint16_t(a + 1), uint16_t(a + 1),
                                                           uint16_t(a + 2), uint16_t(a + 3)});
        }
        object.vertex_count = static_cast<uint32_t>(vertices.size() / 36) - object.first_vertex;
        object.triangle_count = static_cast<uint32_t>(model.triangles.size() / 3) - object.first_triangle;
        model.objects.push_back(object);
    }
    // A decal object of its own: a quad 2 mm above height `y` (over another object's flat surface).
    void decal(float x, float z, float y)
    {
        MeshObject object;
        object.first_vertex = static_cast<uint32_t>(vertices.size() / 36);
        object.first_triangle = static_cast<uint32_t>(model.triangles.size() / 3);
        vertex(x, y + 0.002f, z, 0, 0), vertex(x + 3, y + 0.002f, z, 1, 0), vertex(x, y + 0.002f, z + 3, 0, 1),
            vertex(x + 3, y + 0.002f, z + 3, 1, 1);
        model.triangles.insert(model.triangles.end(), {0, 2, 1, 1, 2, 3});
        object.vertex_count = 4;
        object.triangle_count = 2;
        model.objects.push_back(object);
    }
    Model done()
    {
        model.buffers = {vertices};
        model.strides = {36};
        return model;
    }
};

void synthetic()
{
    {
        // Two neighbouring bumpy grids (two objects meeting along x = 100, copies of the same positions), one large
        // enough for several clusters, and a decal pair.
        Builder b;
        b.grid(0, 0, 120, 100.f / 120, 4.f);
        b.grid(100, 0, 40, 100.f / 120, 4.f);
        b.decal_pair(500, 500, 3.f);
        const Model model = b.done();
        const Totals t = check(model);
        const auto views = model.views();
        if (far_lod::Encode(far_lod::Build(model.objects, views, model.triangles)) !=
            far_lod::Encode(far_lod::Build(model.objects, views, model.triangles, threads)))
            fail("the LODs depend on the threads");
        if (t.clusters < 15)
            fail("the large grid was not split into clusters");
        if (t.kept[far_lod::kLevels - 1] > 0.5 * t.triangles)
            fail("the bumpy grids did not simplify");
        std::printf("synthetic: %zu triangles, %zu clusters, kept per level:", t.triangles, t.clusters);
        for (const double k : t.kept)
            std::printf(" %.0f", k);
        std::printf(", %zu bytes encoded\n", t.encoded);
    }
    {
        // A flat grid: every level beyond 0 collapses it to the locked outline at most; with a decal object over it
        // the grid's triangles under the decal stay (verify.hpp checks them at every level).
        Builder b;
        b.grid(0, 0, 30, 1.f, 0.f);
        b.decal(10.5f, 10.5f, -0.002f);
        const Model model = b.done();
        const auto views = model.views();
        const auto ties = far_lod::ModelDepthTies(model.objects, views, model.triangles);
        if (std::ranges::none_of(ties, [](const auto &pair) { return pair.first.object != pair.second.object; }))
            fail("the decal object does not tie with the grid under it");
        const Totals t = check(model);
        if (t.kept[1] >= t.kept[0])
            fail("a flat grid kept all its triangles at level 1");
    }
    {
        // A broken layout is refused.
        Builder b;
        b.grid(0, 0, 4, 1.f, 0.f);
        Model model = b.done();
        model.objects[0].vertex_count -= 1;
        if (!far_lod::CheckMeshLayout(model.objects, model.counts(), model.triangles))
            fail("CheckMeshLayout took an index outside its object");
    }
    std::printf("synthetic models: passed\n");
}
} // namespace

int main()
{
    try
    {
        synthetic();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
}
