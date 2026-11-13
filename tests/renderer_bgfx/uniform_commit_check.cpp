// Uniform commits of the GL backend (patches/bgfx-gl-uniform-commit.patch): a program gets only the uniforms whose
// value differs from what it holds.
//
// Draws a 4x4 grid of quads through the shim on OpenGL ES in a small SDL window (an X display, xvfb-run in CI). Each
// quad's colour is D3DRS_TEXTUREFACTOR (fragment uniform u_ffFs[0]), and the quads switch between three programs
// (unlit, lit, and the alpha-tested fragment shader instead of its opaque twin) in an order that changes the colour
// while another program is bound, switches back without a change, returns to a value a program already holds and
// repeats unchanged. Every quad must show its own colour in three frames in a row. Then a frame that alternates the
// programs with one colour must upload almost nothing: every program already holds every value (before the patch
// each switch re-uploaded the program's whole uniform block).
#include "device.hpp"
#include <SDL2/SDL.h>

#include "storm/render/perf_stats.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kSize = 64;
constexpr uint32_t kGrid = 4;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

enum Program
{
    Unlit,
    Lit,
    AlphaTested,
};

struct Quad
{
    Program program;
    uint32_t color; // A8R8G8B8
};

constexpr uint32_t kRed = 0xFFE01010, kGreen = 0xFF10C020, kBlue = 0xFF2030F0, kGrey = 0xFF808080;

// Program and colour per quad in draw order (row by row).
const Quad kMixed[kGrid * kGrid] = {
    {Unlit, kRed},        {Lit, kGreen},        {Unlit, kGreen}, // the colour changed while Lit was bound
    {Unlit, kGreen},                                              // repeated, nothing changes
    {Lit, kRed},          {AlphaTested, kRed},  {Unlit, kRed},   // switches only: every program must get kRed
    {Lit, kBlue},         {Lit, kRed},                            // back to a value Lit held before kBlue
    {AlphaTested, kGrey}, {Unlit, kBlue},       {Lit, kBlue},
    {AlphaTested, kBlue}, {AlphaTested, kGrey}, {Unlit, kGrey},  {Lit, kGreen},
};

struct Vertex
{
    float x, y, z;
    float nx, ny, nz;
    uint32_t diffuse;
};

struct Frame
{
    std::vector<uint8_t> pixels;
    uint64_t gl_uniform_calls = 0, gl_uniform_bytes = 0;
};

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

    Frame render(const Quad (&quads)[kGrid * kGrid])
    {
        const render::PerfCounters start = render::PerfTotals();
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
        d_.SetRenderState(D3DRS_ALPHAREF, 0x10);
        d_.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d_.SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
        d_.SetTexture(0, nullptr);
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
        const float cell = 2.0f / kGrid;
        for (uint32_t i = 0; i < kGrid * kGrid; ++i)
        {
            const float x0 = -1.0f + cell * static_cast<float>(i % kGrid), y0 = 1.0f - cell * static_cast<float>(i / kGrid);
            const float x1 = x0 + cell, y1 = y0 - cell;
            const Vertex v[6] = {{x0, y0, 0.5f, 0, 0, -1, 0xFFFFFFFF}, {x1, y0, 0.5f, 0, 0, -1, 0xFFFFFFFF},
                                 {x0, y1, 0.5f, 0, 0, -1, 0xFFFFFFFF}, {x0, y1, 0.5f, 0, 0, -1, 0xFFFFFFFF},
                                 {x1, y0, 0.5f, 0, 0, -1, 0xFFFFFFFF}, {x1, y1, 0.5f, 0, 0, -1, 0xFFFFFFFF}};
            d_.SetRenderState(D3DRS_LIGHTING, quads[i].program == Lit ? TRUE : FALSE);
            d_.SetRenderState(D3DRS_ALPHATESTENABLE, quads[i].program == AlphaTested ? TRUE : FALSE);
            d_.SetRenderState(D3DRS_TEXTUREFACTOR, quads[i].color);
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(Vertex)), "DrawPrimitiveUP");
        }
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(target_, readback_), "GetRenderTargetData");
        Frame frame;
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        frame.pixels.resize(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&frame.pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget(back)");
        check(d_.Present(), "Present");
        // Present ran bgfx::frame(): this frame's commands are executed and counted.
        const render::PerfCounters done = render::PerfTotals().Since(start);
        frame.gl_uniform_calls = done.gl_uniform_calls;
        frame.gl_uniform_bytes = done.gl_uniform_bytes;
        return frame;
    }

  private:
    Device &d_;
    storm::shim::Surface *target_ = nullptr, *readback_ = nullptr, *back_ = nullptr;
};

// Every quad's centre pixel against its colour (within 2/255 per channel).
bool compare(const char *name, const Frame &frame, const Quad (&quads)[kGrid * kGrid])
{
    bool ok = true;
    for (uint32_t i = 0; i < kGrid * kGrid; ++i)
    {
        const uint32_t px = (i % kGrid) * (kSize / kGrid) + kSize / kGrid / 2;
        const uint32_t py = (i / kGrid) * (kSize / kGrid) + kSize / kGrid / 2;
        const uint8_t *p = &frame.pixels[(py * kSize + px) * 4]; // B, G, R, A
        const uint32_t c = quads[i].color;
        const int expected[3] = {int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF)};
        for (int k = 0; k < 3; ++k)
            if (std::abs(int(p[k]) - expected[k]) > 2)
            {
                std::printf("FAIL %s: quad %u shows %02x%02x%02x, expected %06x\n", name, i, p[2], p[1], p[0],
                            c & 0xFFFFFF);
                ok = false;
                break;
            }
    }
    std::printf("%s %s: %llu glUniform calls, %llu bytes\n", ok ? "PASS" : "FAIL", name,
                static_cast<unsigned long long>(frame.gl_uniform_calls),
                static_cast<unsigned long long>(frame.gl_uniform_bytes));
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
    SDL_Window *window = SDL_CreateWindow("uniform_commit_check", 0, 0, kSize, kSize, 0);
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
            const char *names[3] = {"mixed, frame 1", "mixed, frame 2", "mixed, frame 3"};
            for (const char *name : names)
                ok &= compare(name, harness.render(kMixed), kMixed);

            Quad alternating[kGrid * kGrid];
            for (uint32_t i = 0; i < kGrid * kGrid; ++i)
                alternating[i] = {static_cast<Program>(i % 3), kGreen};
            harness.render(alternating); // every program gets kGreen once
            const Frame frame = harness.render(alternating);
            ok &= compare("alternating programs, one colour", frame, alternating);
            // Left to upload: bgfx's per-draw transform (u_model, predefined: 64 bytes a draw) and the few values the
            // frame's own clear and target changes set. The whole uniform block of each switch would be ~20 KB.
            constexpr uint64_t kLimit = kGrid * kGrid * 64 + 1024;
            if (frame.gl_uniform_bytes > kLimit)
            {
                std::printf("FAIL alternating programs: %llu bytes uploaded, at most %llu expected (program switches "
                            "re-upload values the programs already hold)\n",
                            static_cast<unsigned long long>(frame.gl_uniform_bytes),
                            static_cast<unsigned long long>(kLimit));
                ok = false;
            }
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: uniform commits keep every program's values\n" : "FAIL: uniform commits\n");
    return ok ? 0 : 1;
}
