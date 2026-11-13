// The save thumbnail when the app goes to the background (Device::GetRenderTargetData, copy_frame), through the shim
// in an SDL window (an X display, like skinning_check), on OpenGL ES and on Vulkan (run.py).
//
// Android destroys the surface before the game writes its background autosave; the save's screenshot
// (scr_shoter.cpp, Renderer::GetRenderTargetAsTexture) reads the back buffer then, where no read-back can run. The
// device keeps a small copy of the presented frame for that: the read without a surface must return the last frame
// (top red, bottom blue here), the read with a surface the frame itself, and a read of another render target without
// a surface must still fail.
#include "device.hpp"

#include "check_backend.hpp"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kWidth = 160, kHeight = 96;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

void frame(Device &d)
{
    check(d.BeginScene(), "BeginScene");
    check(d.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFF0000, 1.0f, 0), "Clear (red)");
    // The bottom half blue: a screen-space quad
    struct Vertex
    {
        float x, y, z, rhw;
        uint32_t color;
    };
    const float w = static_cast<float>(kWidth), h = static_cast<float>(kHeight);
    const Vertex quad[4] = {{0, h / 2, 0.5f, 1, 0xFF0000FF}, {w, h / 2, 0.5f, 1, 0xFF0000FF}, {0, h, 0.5f, 1, 0xFF0000FF},
                            {w, h, 0.5f, 1, 0xFF0000FF}};
    d.SetRenderState(D3DRS_ZENABLE, FALSE);
    d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d.SetRenderState(D3DRS_LIGHTING, FALSE);
    d.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d.SetTexture(0, nullptr);
    d.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    check(d.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "draw (blue bottom)");
    check(d.EndScene(), "EndScene");
    check(d.Present(), "Present");
}

// The thumbnail's way: a texture of the target's size, its level read from the target (GetRenderTargetAsTexture).
HRESULT read(Device &d, shim::Surface *target, std::vector<uint32_t> &pixels)
{
    D3DSURFACE_DESC desc{};
    check(target->GetDesc(&desc), "GetDesc");
    shim::Texture2D *texture = nullptr;
    check(d.CreateTexture(desc.Width, desc.Height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr),
          "CreateTexture");
    shim::Surface *level = nullptr;
    check(texture->GetSurfaceLevel(0, &level), "GetSurfaceLevel");
    const HRESULT result = d.GetRenderTargetData(target, level);
    if (result == D3D_OK)
    {
        D3DLOCKED_RECT locked;
        check(level->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect");
        pixels.resize(static_cast<size_t>(desc.Width) * desc.Height);
        for (uint32_t y = 0; y < desc.Height; ++y)
            std::memcpy(&pixels[y * desc.Width], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch,
                        desc.Width * 4);
        level->UnlockRect();
    }
    level->Release();
    texture->Release();
    return result;
}

// A8R8G8B8 texels: the top quarter red, the bottom quarter blue.
bool top_red_bottom_blue(const std::vector<uint32_t> &pixels)
{
    if (pixels.size() != static_cast<size_t>(kWidth) * kHeight)
        return false;
    const auto near = [](uint32_t texel, uint32_t want) {
        for (int shift = 0; shift < 24; shift += 8)
            if (std::abs(static_cast<int>((texel >> shift) & 0xFF) - static_cast<int>((want >> shift) & 0xFF)) > 8)
                return false;
        return true;
    };
    uint32_t wrong = 0;
    for (uint32_t y = 0; y < kHeight; ++y)
        for (uint32_t x = 0; x < kWidth; ++x)
            if (y < kHeight / 4)
                wrong += !near(pixels[y * kWidth + x], 0xFFFF0000);
            else if (y >= kHeight * 3 / 4)
                wrong += !near(pixels[y * kWidth + x], 0xFF0000FF);
    std::printf("     %u of %u pixels of the top and bottom quarters differ (first texel %08x, last %08x)\n", wrong,
                kWidth * kHeight / 2, pixels.front(), pixels.back());
    return wrong == 0;
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL video: %s (an X display is needed)\n", SDL_GetError());
        return 1;
    }
    host_check::UseBackendFromEnvironment();
    SDL_Window *window = SDL_CreateWindow("thumbnail_check", 0, 0, kWidth, kHeight, 0);
    shim::Context &context = shim::Context::instance();
    if (!window || !context.init(window, kWidth, kHeight, false))
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        auto *device = new Device(window, params);
        shim::Surface *back = nullptr, *other = nullptr;
        check(device->GetRenderTarget(0, &back), "GetRenderTarget");
        check(device->CreateRenderTarget(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &other, nullptr),
              "CreateRenderTarget");
        for (int i = 0; i < 8; ++i)
            frame(*device);
        std::vector<uint32_t> pixels;
        ok &= expect(read(*device, back, pixels) == D3D_OK && top_red_bottom_blue(pixels),
                     "with a surface the back buffer reads back the frame");
        // Android takes the surface away (surfaceDestroyed); the game saves before it gets a new one.
        context.request_surface_release();
        context.poll_surface();
        ok &= expect(!context.ready(), "the surface is gone");
        pixels.clear();
        ok &= expect(read(*device, back, pixels) == D3D_OK && top_red_bottom_blue(pixels),
                     "without a surface the back buffer reads the copy of the last frame (the save's thumbnail)");
        ok &= expect(read(*device, other, pixels) == D3DERR_DEVICELOST,
                     "without a surface another render target still cannot be read");
        context.request_surface_restore();
        context.poll_surface();
        frame(*device);
        ok &= expect(context.ready(), "the surface comes back");
        other->Release();
        back->Release();
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: thumbnail_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
