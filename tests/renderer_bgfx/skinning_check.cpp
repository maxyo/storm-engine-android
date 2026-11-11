// GPU skinning parity: the skinned fixed-function vertex shader (ff_vertex.sh, FF_SKIN) against
// the CPU kernel it replaced (src/libs/model/src/skinning.hpp).
//
// Renders the same two-bone-weighted mesh twice through the shim on OpenGL ES in a small SDL window
// (the host engine's own path; needs an X display, xvfb-run in CI): once skinned on the CPU into
// GEOS::VERTEX0 vertices, once as
// GEOS::AVERTEX0 vertices with the bone palette (skinning_palette.hpp) in D3DVERTEXTEXTURESAMPLER0.
// Unlit, lit (one directional light, normalised normals) and lit with the effects' per-pixel lighting
// (fx «Высокое»: vs_ff_lit / vs_ff_lit_skin); the read-back pictures must match.
#include "device.hpp"
#include <SDL2/SDL.h>
#include "skinning.hpp"
#include "skinning_palette.hpp"
#include "storm/render/effects.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kSize = 96;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

struct Mesh
{
    std::vector<skinning::Source> source; // GEOS::AVERTEX0
    std::vector<float> bones;             // 16 floats per bone, memory order of glm::mat4 / D3DMATRIX
    uint32_t bone_count = 0;
};

void set_bone(float *m, float angle, float tx, float ty, float scale)
{
    const float c = std::cos(angle) * scale, s = std::sin(angle) * scale;
    const float v[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, scale, 0, tx, ty, 0, 1};
    std::memcpy(m, v, sizeof(v));
}

// A 12x12 grid of triangles in clip space, each vertex weighted between two of three bones.
Mesh make_mesh()
{
    Mesh mesh;
    mesh.bone_count = 3;
    mesh.bones.resize(16 * mesh.bone_count);
    set_bone(&mesh.bones[0], 0.0f, 0.0f, 0.0f, 1.0f);
    set_bone(&mesh.bones[16], 0.35f, 0.1f, -0.05f, 0.9f);
    set_bone(&mesh.bones[32], -0.5f, -0.12f, 0.08f, 1.1f);
    const int n = 12;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
            for (const auto &corner : corners)
            {
                const float fx = (x + corner[0]) / n, fy = (y + corner[1]) / n;
                skinning::Source v{};
                v.pos[0] = -0.6f + 1.2f * fx;
                v.pos[1] = -0.6f + 1.2f * fy;
                v.pos[2] = 0.5f;
                v.weight = fx;
                const uint32_t b0 = (x + y) % 3, b1 = (x + 2 * y + 1) % 3;
                v.boneid = b0 | (b1 << 8);
                v.nrm[0] = 0.3f * (fx - 0.5f);
                v.nrm[1] = 0.2f;
                v.nrm[2] = -1.0f;
                v.color = static_cast<int32_t>(0xFF000000u | (uint32_t(fx * 255) << 16) | (uint32_t(fy * 255) << 8) | 0x40u);
                mesh.source.push_back(v);
            }
        }
    return mesh;
}

class Harness
{
  public:
    explicit Harness(Device &device) : d_(device)
    {
        check(d_.CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target_, nullptr),
              "CreateRenderTarget");
        check(d_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Harness()
    {
        target_->Release();
        readback_->Release();
        back_->Release();
    }

    std::vector<uint8_t> render(const Mesh &mesh, bool gpu, bool lit)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, target_), "SetRenderTarget");
        check(d_.SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        d_.SetTransform(D3DTS_WORLD, &identity);
        d_.SetTransform(D3DTS_VIEW, &identity);
        d_.SetTransform(D3DTS_PROJECTION, &identity);
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, lit ? TRUE : FALSE);
        d_.SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        if (lit)
        {
            D3DLIGHT9 light{};
            light.Type = D3DLIGHT_DIRECTIONAL;
            light.Diffuse = {0.9f, 0.8f, 0.7f, 1};
            light.Ambient = {0.1f, 0.1f, 0.1f, 1};
            light.Direction = {0.3f, -0.4f, 1.0f};
            d_.SetLight(0, &light);
            d_.LightEnable(0, TRUE);
        }
        const UINT triangles = static_cast<UINT>(mesh.source.size() / 3);
        if (gpu)
        {
            IDirect3DTexture9 *palette = nullptr;
            check(d_.CreateTexture(model::kPaletteTexelsPerBone, mesh.bone_count, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F,
                                   D3DPOOL_DEFAULT, &palette, nullptr),
                  "CreateTexture(palette)");
            D3DLOCKED_RECT locked;
            check(palette->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD), "LockRect(palette)");
            for (uint32_t i = 0; i < mesh.bone_count; ++i)
                model::writePaletteRow(&mesh.bones[16 * i],
                                       reinterpret_cast<float *>(static_cast<uint8_t *>(locked.pBits) + i * locked.Pitch));
            palette->UnlockRect(0);
            d_.SetTexture(D3DVERTEXTEXTURESAMPLER0, palette);
            d_.SetFVF(model::kSkinnedSourceFvf);
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, mesh.source.data(), model::kSkinnedSourceStride),
                  "draw (GPU skinning)");
            d_.SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr);
            palette->Release();
        }
        else
        {
            std::vector<skinning::Target> skinned(mesh.source.size());
            skinning::skinScalar(mesh.source.data(), skinned.data(), mesh.bones.data(), 16 * sizeof(float),
                                 mesh.source.size());
            d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, skinned.data(), sizeof(skinning::Target)),
                  "draw (CPU skinning)");
        }
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(target_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        std::vector<uint8_t> pixels(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget(back)");
        check(d_.Present(nullptr, nullptr, nullptr, nullptr), "Present");
        return pixels;
    }

  private:
    Device &d_;
    IDirect3DSurface9 *target_ = nullptr, *readback_ = nullptr, *back_ = nullptr;
};

// Pixels whose colour differs by more than 2/255 in a channel, and how many pixels were drawn.
bool compare(const char *name, const std::vector<uint8_t> &cpu, const std::vector<uint8_t> &gpu)
{
    uint32_t drawn = 0, different = 0, max_delta = 0;
    for (size_t i = 0; i < cpu.size(); i += 4)
    {
        drawn += (cpu[i] | cpu[i + 1] | cpu[i + 2]) != 0;
        uint32_t delta = 0;
        for (int c = 0; c < 3; ++c)
            delta = std::max<uint32_t>(delta, std::abs(int(cpu[i + c]) - int(gpu[i + c])));
        max_delta = std::max(max_delta, delta);
        different += delta > 2;
    }
    const uint32_t total = kSize * kSize;
    // Edge pixels may flip where a vertex lands on a pixel centre (float order differs in the shader).
    const bool ok = drawn > total / 5 && different <= total / 200;
    std::printf("%s %s: %u of %u pixels drawn, %u differ by more than 2/255 (max %u)\n", ok ? "PASS" : "FAIL", name,
                drawn, total, different, max_delta);
    return ok;
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL video: %s (an X display is needed)\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("skinning_check", 0, 0, kSize, kSize, 0);
    if (!window || !shim::Context::instance().init(window, kSize, kSize, false))
    {
        std::fprintf(stderr, "FAIL: no OpenGL ES window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        auto *device = new Device(window, params);
        {
            Harness harness(*device);
            const Mesh mesh = make_mesh();
            std::vector<uint8_t> vertex_lit;
            for (const bool lit : {false, true})
            {
                const auto cpu = harness.render(mesh, false, lit);
                const auto gpu = harness.render(mesh, true, lit);
                ok &= compare(lit ? "lit" : "unlit", cpu, gpu);
                if (lit)
                    vertex_lit = cpu;
            }
            // fx «Высокое»: lit draws go through the effects' per-pixel variant (vs_ff_lit / vs_ff_lit_skin).
            // The previous settings draw until the new ones are prepared (a few frames).
            render::ConfigureEffects(2, {});
            for (int frame = 0; frame < 120 && render::CurrentEffects().pending; ++frame)
                harness.render(mesh, false, true);
            if (render::CurrentEffects().pending)
            {
                std::printf("FAIL lit, fx per-pixel: «Высокое» still pending after 120 frames\n");
                ok = false;
            }
            const auto cpu = harness.render(mesh, false, true);
            const auto gpu = harness.render(mesh, true, true);
            render::ConfigureEffects(0, {});
            ok &= compare("lit, fx per-pixel", cpu, gpu);
            if (cpu == vertex_lit)
            {
                std::printf("FAIL lit, fx per-pixel: the picture equals vertex lighting, the fx variant did not run\n");
                ok = false;
            }
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: GPU skinning matches the CPU kernel\n" : "FAIL: GPU skinning differs from the CPU kernel\n");
    return ok ? 0 : 1;
}
