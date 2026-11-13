// The engine's texture formats on the GPU (OpenGL ES in an SDL window: needs an X display, e.g. xvfb-run): a 2D
// texture in every colour format of texture/core d3d_format, with a partial mip chain (created through the DDS
// container bimg writes), is drawn 1:1 (point sampling) into a render target and read back (GetRenderTargetData converts with bimg)
// into an A8R8G8B8 and an R5G6B5 surface. The picture must be the texels as texture::decode_rgba8 reads them: the
// D3D bytes uploaded natively (or converted by bgfx where the GPU lacks the format), the X formats with alpha 1.
// Then the texture is locked and rewritten (the shim's update path) and must show the new texels.
#include "device.hpp"
#include "texture/core/formats.hpp"

#include <SDL2/SDL.h>
#include <bx/allocator.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
using storm::shim::Device;

namespace
{
constexpr uint32_t kSize = 16, kLevels = 3; // a full chain would have 5 levels

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

// Level 0 texels of `format`, a different pattern per `seed`: every byte varies, so channel swaps show.
std::vector<uint8_t> pattern(D3DFORMAT format, uint32_t seed)
{
    std::vector<uint8_t> bytes(texture::d3d_level_bytes(static_cast<uint32_t>(format), kSize, kSize));
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>((i * 37 + seed * 101 + (i >> 4) * 13) ^ (i >> 3));
    if (format == D3DFMT_DXT1) // keep every block in its 4-colour mode (no punch-through alpha)
        for (size_t block = 0; block + 8 <= bytes.size(); block += 8)
            if (bytes[block] | bytes[block + 1] << 8 <= (bytes[block + 2] | bytes[block + 3] << 8))
                std::swap(bytes[block], bytes[block + 2]), std::swap(bytes[block + 1], bytes[block + 3]),
                    bytes[block] |= 1;
    return bytes;
}

class Harness
{
  public:
    explicit Harness(Device &device) : d_(device)
    {
        check(d_.CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target_, nullptr),
              "CreateRenderTarget");
        check(d_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface(A8R8G8B8)");
        check(d_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &readback565_, nullptr),
              "CreateOffscreenPlainSurface(R5G6B5)");
    }
    ~Harness()
    {
        target_->Release();
        readback_->Release();
        readback565_->Release();
    }

    // Draws level 0 of `texture` 1:1 and returns the A8R8G8B8 and R5G6B5 readbacks.
    void draw(shim::Texture2D *texture, std::vector<uint8_t> &bgra, std::vector<uint8_t> &rgb565)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, target_), "SetRenderTarget");
        check(d_.SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "Clear");
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d_.SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        d_.SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d_.SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d_.SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d_.SetTexture(0, texture);
        struct Vertex
        {
            float x, y, z, rhw, u, v;
        };
        const float s = static_cast<float>(kSize) - 0.5f, o = -0.5f; // texel centres on pixel centres
        const Vertex quad[4] = {{o, o, 0, 1, 0, 0}, {s, o, 0, 1, 1, 0}, {o, s, 0, 1, 0, 1}, {s, s, 0, 1, 1, 1}};
        d_.SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "draw");
        d_.SetTexture(0, nullptr);
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(target_, readback_), "GetRenderTargetData(A8R8G8B8)");
        check(d_.GetRenderTargetData(target_, readback565_), "GetRenderTargetData(R5G6B5)");
        bgra = read(readback_, 4);
        rgb565 = read(readback565_, 2);
        check(d_.Present(), "Present");
    }

  private:
    static std::vector<uint8_t> read(shim::Surface *surface, uint32_t bpp)
    {
        D3DLOCKED_RECT locked;
        check(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        std::vector<uint8_t> pixels(kSize * kSize * bpp);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&pixels[y * kSize * bpp], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * bpp);
        surface->UnlockRect();
        return pixels;
    }

    Device &d_;
    shim::Surface *target_ = nullptr, *readback_ = nullptr, *readback565_ = nullptr;
};

void fill(shim::Texture2D *texture, D3DFORMAT format, uint32_t seed)
{
    for (uint32_t level = 0; level < kLevels; ++level)
    {
        const uint32_t size = kSize >> level;
        const std::vector<uint8_t> bytes = level == 0 ? pattern(format, seed) : std::vector<uint8_t>(
                                                                                   texture::d3d_level_bytes(static_cast<uint32_t>(format), size, size), 0x55);
        D3DLOCKED_RECT locked;
        check(texture->LockRect(level, &locked, nullptr, 0), "LockRect(texture)");
        std::memcpy(locked.pBits, bytes.data(), bytes.size()); // tightly packed: the shim's pitch is the row size
        texture->UnlockRect(level);
    }
}

// The picture of `bgra` (A8R8G8B8 readback) against the texels; `rgb565` against `bgra` converted by bimg.
bool compare(const char *name, D3DFORMAT format, uint32_t seed, const std::vector<uint8_t> &bgra,
             const std::vector<uint8_t> &rgb565)
{
    std::vector<uint8_t> expected(kSize * kSize * 4);
    texture::decode_rgba8(static_cast<uint32_t>(format), pattern(format, seed).data(), kSize, kSize, expected.data());
    static bx::DefaultAllocator allocator;
    // the R5G6B5 readback is the same picture as the A8R8G8B8 one, converted to the nearest 5/6-bit values
    std::vector<uint8_t> expected565(kSize * kSize * 2);
    bimg::imageConvert(&allocator, expected565.data(), bimg::TextureFormat::B5G6R5, bgra.data(),
                       bimg::TextureFormat::BGRA8, kSize, kSize, 1);
    uint32_t worst = 0, worst565 = 0;
    for (uint32_t i = 0; i < kSize * kSize; ++i)
    {
        const uint8_t got[4] = {bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4 + 0], bgra[i * 4 + 3]};
        for (int c = 0; c < 4; ++c)
            worst = std::max<uint32_t>(worst, std::abs(int(got[c]) - int(expected[i * 4 + c])));
        worst565 = std::max<uint32_t>(worst565, rgb565[i * 2] != expected565[i * 2] || rgb565[i * 2 + 1] != expected565[i * 2 + 1]);
    }
    // 1/255: a GPU may round an 8-bit value through its float pipeline differently by one step.
    const bool ok = worst <= 1 && worst565 == 0;
    std::printf("%s %s: worst channel difference %u/255, R5G6B5 readback %s\n", ok ? "PASS" : "FAIL", name, worst,
                worst565 ? "differs" : "equal");
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
    SDL_Window *window = SDL_CreateWindow("texture_format_check", 0, 0, kSize, kSize, 0);
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
            const struct
            {
                D3DFORMAT format;
                const char *name;
            } formats[] = {{D3DFMT_A8R8G8B8, "A8R8G8B8"}, {D3DFMT_X8R8G8B8, "X8R8G8B8"}, {D3DFMT_R5G6B5, "R5G6B5"},
                           {D3DFMT_A1R5G5B5, "A1R5G5B5"}, {D3DFMT_A4R4G4B4, "A4R4G4B4"}, {D3DFMT_A8B8G8R8, "A8B8G8R8"},
                           {D3DFMT_DXT1, "DXT1"}};
            for (const auto &f : formats)
            {
                shim::Texture2D *texture = nullptr;
                check(device->CreateTexture(kSize, kSize, kLevels, 0, f.format, D3DPOOL_MANAGED, &texture, nullptr),
                      "CreateTexture");
                std::vector<uint8_t> bgra, rgb565;
                fill(texture, f.format, 1);
                harness.draw(texture, bgra, rgb565);
                char name[64];
                std::snprintf(name, sizeof(name), "%s created", f.name);
                ok &= compare(name, f.format, 1, bgra, rgb565);
                if (f.format != D3DFMT_DXT1) // DXT is recreated on a new lock, not updated
                {
                    fill(texture, f.format, 2);
                    harness.draw(texture, bgra, rgb565);
                    std::snprintf(name, sizeof(name), "%s updated", f.name);
                    ok &= compare(name, f.format, 2, bgra, rgb565);
                }
                texture->Release();
            }
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: every texture format reads back as its texels\n" : "FAIL: a texture format reads back wrong\n");
    return ok ? 0 : 1;
}
