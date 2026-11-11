// Draw/state regression check of the bgfx shim (host CI) and its CPU benchmark.
//
// Renders a fixed synthetic frame through the D3D9-shaped device on bgfx's Noop backend
// (Context::init_headless) and compares the shim's per-frame counters
// (storm/render/perf_stats.hpp) with the expected numbers below. A change that makes the
// shim submit more draws, views, state/uniform/texture changes or transient bytes fails here
// without a scene sweep; an improvement fails too, so that the numbers are lowered with it.
//
// The frame mirrors what the game sends: lit static models (world matrix per draw, a few
// textures and materials), a CPU-skinned mesh written every frame, text and sprites as
// DrawPrimitiveUP batches, triangle fans, an every-other-frame rewritten "static" buffer and
// a render-target pass with viewport changes.
//
//   perf_check            check the counters (exit 1 on a difference)
//   perf_check --bench N  render N frames and print the shim's CPU time per frame and per draw
#include "device.hpp"
#include "storm/render/perf_stats.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kWidth = 1280, kHeight = 720;

struct MeshVertex
{
    float x, y, z, nx, ny, nz, u, v;
};
constexpr DWORD kMeshFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

struct ScreenVertex
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
constexpr DWORD kScreenFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct ColorVertex
{
    float x, y, z;
    DWORD color;
};
constexpr DWORD kColorFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

D3DMATRIX translation(float x, float y, float z)
{
    D3DMATRIX m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    m.m[3][0] = x, m.m[3][1] = y, m.m[3][2] = z;
    return m;
}

D3DMATRIX perspective()
{
    D3DMATRIX m{};
    m.m[0][0] = 1.0f, m.m[1][1] = 1.777f, m.m[2][2] = 1.0001f, m.m[2][3] = 1.0f, m.m[3][2] = -0.1f;
    return m;
}

class Scene
{
  public:
    explicit Scene(Device &device) : d_(device)
    {
        for (auto &texture : textures_)
            texture = make_texture(64);
        check(d_.CreateTexture(256, 256, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &target_, nullptr),
              "CreateTexture(rt)");
        check(target_->GetSurfaceLevel(0, &target_surface_), "GetSurfaceLevel");
        check(d_.CreateDepthStencilSurface(256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &target_depth_, nullptr),
              "CreateDepthStencilSurface");
        check(d_.GetRenderTarget(0, &back_surface_), "GetRenderTarget");
        check(d_.GetDepthStencilSurface(&back_depth_), "GetDepthStencilSurface");

        // A 24-quad static model.
        std::vector<MeshVertex> vertices;
        std::vector<uint16_t> indices;
        for (uint16_t q = 0; q < 24; ++q)
        {
            const float x = static_cast<float>(q % 6), y = static_cast<float>(q / 6);
            const uint16_t base = static_cast<uint16_t>(vertices.size());
            vertices.push_back({x, y, 0, 0, 0, -1, 0, 0});
            vertices.push_back({x + 1, y, 0, 0, 0, -1, 1, 0});
            vertices.push_back({x, y + 1, 0, 0, 0, -1, 0, 1});
            vertices.push_back({x + 1, y + 1, 0, 0, 0, -1, 1, 1});
            for (uint16_t i : {0, 1, 2, 2, 1, 3})
                indices.push_back(static_cast<uint16_t>(base + i));
        }
        mesh_vertices_ = static_cast<UINT>(vertices.size());
        mesh_triangles_ = static_cast<UINT>(indices.size() / 3);
        model_vb_ = make_vb(vertices, 0);
        skinned_vb_ = make_vb(vertices, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY);
        rewritten_vb_ = make_vb(vertices, 0);
        check(d_.CreateIndexBuffer(static_cast<UINT>(indices.size() * 2), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib_,
                                   nullptr),
              "CreateIndexBuffer");
        void *data = nullptr;
        check(ib_->Lock(0, 0, &data, 0), "Lock(ib)");
        std::memcpy(data, indices.data(), indices.size() * 2);
        ib_->Unlock();
        source_ = vertices;
    }

    ~Scene()
    {
        for (auto *texture : textures_)
            texture->Release();
        target_surface_->Release();
        target_depth_->Release();
        back_surface_->Release();
        back_depth_->Release();
        target_->Release();
        model_vb_->Release();
        skinned_vb_->Release();
        rewritten_vb_->Release();
        ib_->Release();
    }

    void frame(uint32_t index)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF102030, 1.0f, 0), "Clear");
        const D3DMATRIX view = translation(0, 0, 10), proj = perspective();
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &proj);
        sample_texture();
        lit_models();
        skinned_meshes(index);
        rewritten_buffer(index);
        render_target_pass();
        fans();
        sprites_and_text();
        check(d_.EndScene(), "EndScene");
        check(d_.Present(), "Present");
    }

    UINT draws_per_frame() const { return 40 + 10 + 2 + 3 + 6 + 36; }

  private:
    storm::shim::Texture2D *make_texture(UINT size)
    {
        storm::shim::Texture2D *texture = nullptr;
        check(d_.CreateTexture(size, size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
        D3DLOCKED_RECT rect;
        check(texture->LockRect(0, &rect, nullptr, 0), "LockRect");
        for (UINT y = 0; y < size; ++y)
            std::memset(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch, static_cast<int>(y * 3), size * 4);
        texture->UnlockRect(0);
        return texture;
    }

    storm::shim::VertexBuffer *make_vb(const std::vector<MeshVertex> &vertices, DWORD usage)
    {
        storm::shim::VertexBuffer *vb = nullptr;
        const UINT bytes = static_cast<UINT>(vertices.size() * sizeof(MeshVertex));
        check(d_.CreateVertexBuffer(bytes, usage, kMeshFvf, D3DPOOL_MANAGED, &vb, nullptr), "CreateVertexBuffer");
        void *data = nullptr;
        check(vb->Lock(0, 0, &data, 0), "Lock(vb)");
        std::memcpy(data, vertices.data(), bytes);
        vb->Unlock();
        return vb;
    }

    void model_state()
    {
        d_.SetRenderState(D3DRS_LIGHTING, TRUE);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        d_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        d_.SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d_.SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d_.SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        D3DLIGHT9 light{};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse = {1, 0.9f, 0.8f, 1};
        light.Direction = {0.3f, -1, 0.2f};
        d_.SetLight(0, &light);
        d_.LightEnable(0, TRUE);
        light.Type = D3DLIGHT_POINT;
        light.Position = {2, 3, 4};
        light.Range = 20;
        light.Attenuation1 = 0.1f;
        d_.SetLight(1, &light);
        d_.LightEnable(1, TRUE);
        d_.SetVertexShader(nullptr);
        d_.SetPixelShader(nullptr);
        d_.SetFVF(kMeshFvf);
        d_.SetIndices(ib_);
    }

    // The sun fade reads a sky pixel on the CPU every frame (Renderer::sampleTextureArgb): a
    // read-only lock must not re-upload the texture.
    void sample_texture()
    {
        D3DLOCKED_RECT rect;
        check(textures_[0]->LockRect(0, &rect, nullptr, D3DLOCK_READONLY), "LockRect(read-only)");
        sampled_ += static_cast<const uint8_t *>(rect.pBits)[0];
        textures_[0]->UnlockRect(0);
    }

    // 40 lit models, one world matrix each, 4 textures, a material change every other model.
    void lit_models()
    {
        model_state();
        d_.SetStreamSource(0, model_vb_, 0, sizeof(MeshVertex));
        for (uint32_t i = 0; i < 40; ++i)
        {
            const D3DMATRIX world = translation(static_cast<float>(i % 8), static_cast<float>(i / 8), 0);
            d_.SetTransform(D3DTS_WORLD, &world);
            d_.SetTexture(0, textures_[i % 4]);
            D3DMATERIAL9 material{};
            material.Diffuse = {1, 1, 1, 1};
            material.Ambient = {(i / 2) % 2 ? 0.5f : 0.3f, 0.3f, 0.3f, 1};
            d_.SetMaterial(&material);
            check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh_vertices_, 0, mesh_triangles_), "draw model");
        }
    }

    // A character: skinned on the CPU into a dynamic buffer every frame.
    void skinned_meshes(uint32_t index)
    {
        void *data = nullptr;
        check(skinned_vb_->Lock(0, 0, &data, D3DLOCK_DISCARD), "Lock(skinned)");
        auto *out = static_cast<MeshVertex *>(data);
        for (size_t i = 0; i < source_.size(); ++i)
        {
            out[i] = source_[i];
            out[i].z = 0.01f * static_cast<float>((index + i) % 7);
        }
        skinned_vb_->Unlock();
        d_.SetStreamSource(0, skinned_vb_, 0, sizeof(MeshVertex));
        const D3DMATRIX world = translation(-3, 0, 2);
        d_.SetTransform(D3DTS_WORLD, &world);
        // A whole-mesh pass (shadow/reflection), the 8 subsets, then the whole mesh again.
        check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh_vertices_, 0, mesh_triangles_), "draw skinned pass");
        for (uint32_t subset = 0; subset < 8; ++subset)
        {
            d_.SetTexture(0, textures_[subset % 2]);
            check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh_vertices_, subset * 9, 3), "draw skinned");
        }
        check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh_vertices_, 0, mesh_triangles_), "draw skinned pass");
    }

    // A managed buffer the game rewrites now and then (every other frame here), drawn twice.
    void rewritten_buffer(uint32_t index)
    {
        if (index % 2 == 0)
        {
            void *data = nullptr;
            check(rewritten_vb_->Lock(0, 0, &data, 0), "Lock(rewritten)");
            static_cast<MeshVertex *>(data)[0].x = static_cast<float>(index % 3);
            rewritten_vb_->Unlock();
        }
        d_.SetStreamSource(0, rewritten_vb_, 0, sizeof(MeshVertex));
        for (uint32_t i = 0; i < 2; ++i)
        {
            const D3DMATRIX world = translation(4, static_cast<float>(i), 1);
            d_.SetTransform(D3DTS_WORLD, &world);
            check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh_vertices_, 0, mesh_triangles_), "draw rewritten");
        }
    }

    // A reflection-like pass: own target, clear, 3 draws; the game resets the viewport twice
    // around it without drawing in between.
    void render_target_pass()
    {
        check(d_.SetRenderTarget(0, target_surface_), "SetRenderTarget(rt)");
        check(d_.SetDepthStencilSurface(target_depth_), "SetDepthStencilSurface(rt)");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0), "Clear(rt)");
        d_.SetStreamSource(0, model_vb_, 0, sizeof(MeshVertex));
        for (uint32_t i = 0; i < 3; ++i)
        {
            const D3DMATRIX world = translation(static_cast<float>(i), 0, 3);
            d_.SetTransform(D3DTS_WORLD, &world);
            check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh_vertices_, 0, mesh_triangles_), "draw rt");
        }
        check(d_.SetRenderTarget(0, back_surface_), "SetRenderTarget(back)");
        check(d_.SetDepthStencilSurface(back_depth_), "SetDepthStencilSurface(back)");
        D3DVIEWPORT9 viewport{0, 0, kWidth / 2, kHeight, 0, 1};
        d_.SetViewport(&viewport);
        viewport.Width = kWidth;
        d_.SetViewport(&viewport);
    }

    // Unlit coloured fans (sun glow, flares).
    void fans()
    {
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTexture(0, nullptr);
        d_.SetFVF(kColorFvf);
        const D3DMATRIX world = translation(0, 0, 5);
        d_.SetTransform(D3DTS_WORLD, &world);
        ColorVertex fan[10];
        for (uint32_t i = 0; i < 6; ++i)
        {
            for (uint32_t v = 0; v < 10; ++v)
                fan[v] = {static_cast<float>(v % 3), static_cast<float>(v / 3), static_cast<float>(i), 0x80FFFFFFu};
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 8, fan, sizeof(ColorVertex)), "draw fan");
        }
    }

    // The interface: 12 sprites and 24 text strings as screen-space DrawPrimitiveUP lists.
    void sprites_and_text()
    {
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetFVF(kScreenFvf);
        ScreenVertex quads[6 * 16];
        for (uint32_t i = 0; i < 36; ++i)
        {
            const bool text = i >= 12;
            const uint32_t glyphs = text ? 16 : 1;
            for (uint32_t g = 0; g < glyphs; ++g)
                for (uint32_t c = 0; c < 6; ++c)
                {
                    static const float corner[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                    const float x = static_cast<float>(i * 20 + g * 8) + corner[c][0] * 8;
                    const float y = static_cast<float>(i * 10) + corner[c][1] * 12;
                    quads[g * 6 + c] = {x, y, 0, 1, 0xFFFFFFFFu, corner[c][0], corner[c][1]};
                }
            d_.SetTexture(0, text ? textures_[3] : textures_[i % 3]);
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, glyphs * 2, quads, sizeof(ScreenVertex)), "draw ui");
        }
    }

    Device &d_;
    storm::shim::Texture2D *textures_[4] = {};
    storm::shim::Texture2D *target_ = nullptr;
    storm::shim::Surface *target_surface_ = nullptr, *target_depth_ = nullptr;
    storm::shim::Surface *back_surface_ = nullptr, *back_depth_ = nullptr;
    storm::shim::VertexBuffer *model_vb_ = nullptr, *skinned_vb_ = nullptr, *rewritten_vb_ = nullptr;
    storm::shim::IndexBuffer *ib_ = nullptr;
    std::vector<MeshVertex> source_;
    UINT mesh_vertices_ = 0, mesh_triangles_ = 0;
    uint32_t sampled_ = 0;
};

struct Expected
{
    const char *name;
    uint64_t render::PerfCounters::*field;
    uint64_t value; // per two frames (the rewritten buffer alternates)
};

// Counters of frames 3 and 4 together. Lower them when an optimisation lands.
const Expected kExpected[] = {
    {"draws", &render::PerfCounters::draws, 194},
    {"skipped_draws", &render::PerfCounters::skipped_draws, 0},
    {"views", &render::PerfCounters::views, 6},
    {"program_changes", &render::PerfCounters::program_changes, 4},
    {"state_changes", &render::PerfCounters::state_changes, 6},
    {"uniform_updates", &render::PerfCounters::uniform_updates, 82},
    {"uniform_bytes", &render::PerfCounters::uniform_bytes, 6784},
    {"uniform_draws", &render::PerfCounters::uniform_draws, 46},
    {"uniform_commit_bytes", &render::PerfCounters::uniform_commit_bytes, 75360},
    {"texture_binds", &render::PerfCounters::texture_binds, 182},
    {"texture_changes", &render::PerfCounters::texture_changes, 122},
    {"transient_vb_bytes", &render::PerfCounters::transient_vb_bytes, 144192},
    {"transient_ib_bytes", &render::PerfCounters::transient_ib_bytes, 828},
    {"fan_draws", &render::PerfCounters::fan_draws, 12},
    {"buffer_uploads", &render::PerfCounters::buffer_uploads, 1},
    {"buffer_upload_bytes", &render::PerfCounters::buffer_upload_bytes, 3072},
    {"texture_uploads", &render::PerfCounters::texture_uploads, 0},
};

} // namespace

int main(int argc, char **argv)
{
    uint32_t bench_frames = 0;
    if (argc == 3 && std::strcmp(argv[1], "--bench") == 0)
        bench_frames = static_cast<uint32_t>(std::atoi(argv[2]));
    else if (argc != 1)
    {
        std::fprintf(stderr, "usage: %s [--bench FRAMES]\n", argv[0]);
        return 2;
    }
    if (!shim::Context::instance().init_headless(kWidth, kHeight))
    {
        std::fprintf(stderr, "FAIL: headless bgfx did not start\n");
        return 1;
    }
    int status = 0;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        auto *device = new Device(nullptr, params);
        {
            Scene scene(*device);
            // Frames 1-2 create programs, layouts, buffers and textures.
            scene.frame(1);
            scene.frame(2);
            const render::PerfCounters start = render::PerfTotals();
            scene.frame(3);
            scene.frame(4);
            const render::PerfCounters window = render::PerfTotals().Since(start);
            if (bench_frames)
            {
                const render::PerfCounters bench_start = render::PerfTotals();
                const auto begin = std::chrono::steady_clock::now();
                for (uint32_t i = 0; i < bench_frames; ++i)
                    scene.frame(5 + i);
                const double total_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                const render::PerfCounters bench = render::PerfTotals().Since(bench_start);
                std::printf("bench: %u frames, %u draws/frame: frame %.1f us, shim draw path %.1f us/frame = %.0f ns/draw\n",
                            bench_frames, scene.draws_per_frame(), 1000.0 * total_ms / bench_frames,
                            1000.0 * bench.shim_ms / bench_frames, 1e6 * bench.shim_ms / static_cast<double>(bench.draws));
            }
            std::printf("frames 3-4: %s\n", window.Summary().c_str());
            for (const Expected &entry : kExpected)
            {
                const uint64_t actual = window.*entry.field;
                if (actual != entry.value)
                {
                    std::printf("%s %s: %llu, expected %llu\n", actual > entry.value ? "REGRESSION" : "IMPROVED",
                                entry.name, static_cast<unsigned long long>(actual),
                                static_cast<unsigned long long>(entry.value));
                    status = 1;
                }
            }
        }
        device->Release();
    }
    if (status)
        std::printf("FAIL: renderer counters differ from tests/renderer_bgfx/perf_check.cpp (update kExpected "
                    "when the change is intended)\n");
    else
        std::printf("PASS: renderer counters of the synthetic frame\n");
    return status;
}
