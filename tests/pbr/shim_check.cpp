// The PBR hook of the bgfx shim on bgfx's Noop backend (no GPU): which program a fixed-function draw gets with and
// without a Caribbean Legend PBR material, and that a PBR draw without lighting is skipped (reported once).
#include "device.hpp"
#include "pbr/host.hpp"
#include "storm/render/pbr.hpp"
#include "storm/render/perf_stats.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace storm;

namespace
{
int failures = 0;

void Check(bool condition, const char *what)
{
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    failures += !condition;
}

struct Vertex
{
    float x, y, z, nx, ny, nz;
    float u, v;
};
constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

IDirect3DTexture9 *Texture(shim::Device &d)
{
    IDirect3DTexture9 *texture = nullptr;
    d.CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    D3DLOCKED_RECT rect;
    texture->LockRect(0, &rect, nullptr, 0);
    for (int y = 0; y < 4; ++y)
        std::memset(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch, 0x80, 16);
    texture->UnlockRect(0);
    return texture;
}
} // namespace

int main()
{
    if (!shim::Context::instance().init_headless(320, 240))
    {
        std::printf("FAIL: headless bgfx did not start\n");
        return 1;
    }
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = 320;
        params.BackBufferHeight = 240;
        auto *device = new shim::Device(nullptr, params);
        IDirect3DTexture9 *albedo = Texture(*device), *rma = Texture(*device), *normal = Texture(*device);
        const std::vector<Vertex> triangle = {{0, 0, 1, 0, 0, -1, 0, 0}, {1, 0, 1, 0, 0, -1, 1, 0}, {0, 1, 1, 0, 0, -1, 0, 1}};
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        D3DMATRIX proj = identity;
        proj.m[2][3] = 1.0f, proj.m[3][3] = 0.0f;
        const bgfx::ProgramHandle pbr_program = pbr_host::Host::instance().renderer().Program(pbr::Variant::Standard);
        Check(pbr_host::Host::instance().renderer().ready() && bgfx::isValid(pbr_program), "PBR programs created with the device");

        render::PbrMaterial material;
        material.name = "check:object";
        material.desc.variant = pbr::Variant::Standard;
        material.textures[pbr::kUnitRma] = rma;
        material.textures[pbr::kUnitNormal] = normal;

        uint16_t program = UINT16_MAX;
        const auto draw = [&](const render::PbrMaterial *m) {
            device->BeginScene();
            device->SetTransform(D3DTS_WORLD, &identity);
            device->SetTransform(D3DTS_VIEW, &identity);
            device->SetTransform(D3DTS_PROJECTION, &proj);
            device->SetFVF(kFvf);
            device->SetTexture(0, albedo);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            render::SetPbrMaterial(m);
            const uint64_t before = render::PerfTotals().draws;
            device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle.data(), sizeof(Vertex));
            render::SetPbrMaterial(nullptr);
            const uint64_t drawn = render::PerfTotals().draws - before;
            program = device->last_draw.program; // reset when the frame is presented
            device->EndScene();
            device->Present(nullptr, nullptr, nullptr, nullptr);
            return drawn;
        };

        Check(draw(nullptr) == 1 && program != pbr_program.idx && program != UINT16_MAX, "no material: the emulator's program draws");
        Check(draw(&material) == 0, "PBR material before any lighting preset: the draw is skipped");
        render::PbrLighting lighting;
        lighting.scene = "check";
        lighting.hour.sun = {1, 1, 1};
        lighting.hour.ambient = {0.5f, 0.5f, 0.5f};
        lighting.hour.sun_direction_known = true;
        render::SetPbrLighting(lighting); // no hdr_texture: image-based light off, reported
        Check(draw(&material) == 1 && program == pbr_program.idx, "PBR material + lighting: the PBR program draws");
        Check(draw(nullptr) == 1 && program != pbr_program.idx && program != UINT16_MAX, "material cleared: back to the emulator");
        std::array<float, 3> rgb{};
        Check(!render::PbrPointLightGroup("candle", rgb), "no point group in this hour");

        albedo->Release(), rma->Release(), normal->Release();
        device->Release();
    }
    shim::Context::instance().shutdown();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
