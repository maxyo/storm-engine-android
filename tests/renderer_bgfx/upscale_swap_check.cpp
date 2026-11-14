// The actual Device::draw texture binding across neural-upscale readiness states. GLES/Vulkan draw a patterned
// opaque surface into an offscreen target and compare every transition frame with the base or replacement pixels.
// --noop exercises the same draw/submission/state path without claiming pixel or GPU readiness evidence.
// Models, inference and the generator are intentionally absent: public UpscaleEntry states represent pending work,
// while Read -> Resident and dimension rejection use the real Upscales::end_frame upload path.
#include "device.hpp"
#include "effects/upscale.hpp"
#include "effects/normal_maps.hpp"
#include "storm/render/effects.hpp"
#include "texture/core/cache.hpp"
#include "check_backend.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

using namespace storm;

namespace
{
constexpr uint32_t kSize = 64;
using Pixels = std::vector<uint8_t>;

void require(bool good, const char *what)
{
    if (!good)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

void check(HRESULT result, const char *what)
{
    require(!FAILED(result), what);
}

Pixels pattern(uint32_t size, bool replacement)
{
    Pixels rgba(size * size * 4);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x)
        {
            const bool checker = ((x / (size / 8)) + (y / (size / 8))) % 2;
            const uint8_t r = replacement ? 32 : 192;
            const uint8_t g = replacement ? 192 : 64;
            uint8_t *p = rgba.data() + (y * size + x) * 4;
            p[0] = checker ? r : uint8_t(r + 24);
            p[1] = checker ? g : uint8_t(g + 24);
            p[2] = checker ? 48 : 96;
            p[3] = 255;
        }
    return rgba;
}

shim::Texture2D *make_texture(shim::Device &device, uint32_t size, bool replacement)
{
    shim::Texture2D *texture = nullptr;
    check(device.CreateTexture(size, size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
    const Pixels rgba = pattern(size, replacement);
    D3DLOCKED_RECT locked{};
    check(texture->LockRect(0, &locked, nullptr, 0), "LockRect(texture)");
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x)
        {
            const uint8_t *p = rgba.data() + (y * size + x) * 4;
            uint8_t *q = static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch + x * 4;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = p[3];
        }
    check(texture->UnlockRect(0), "UnlockRect(texture)");
    return texture;
}

struct Frame
{
    uint16_t selected = UINT16_MAX;
    Pixels pixels;
};

class Harness
{
  public:
    Harness(shim::Device &device, bool noop) : device_(device), noop_(noop)
    {
        check(device_.CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target_, nullptr),
              "CreateRenderTarget");
        if (!noop_)
            check(device_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
                  "CreateOffscreenPlainSurface");
    }
    ~Harness()
    {
        target_->Release();
        if (readback_)
            readback_->Release();
    }

    Frame draw(shim::Texture2D *texture, float distance = 0.0f)
    {
        check(device_.BeginScene(), "BeginScene");
        check(device_.SetRenderTarget(0, target_), "SetRenderTarget");
        check(device_.SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        check(device_.Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        for (int i = 0; i < 4; ++i)
            identity.m[i][i] = 1.0f;
        D3DMATRIX world = identity, projection = identity;
        // Shift the world and compensate in the projection: keep the surface visible while its actual draw bounds
        // leave upscale range. This exercises the normal distance/residency path without waiting kForget seconds.
        world.m[3][0] = distance;
        projection.m[3][0] = -distance;
        check(device_.SetTransform(D3DTS_WORLD, &world), "SetTransform(world)");
        check(device_.SetTransform(D3DTS_VIEW, &identity), "SetTransform(view)");
        check(device_.SetTransform(D3DTS_PROJECTION, &projection), "SetTransform(projection)");
        device_.SetRenderState(D3DRS_ZENABLE, FALSE);
        device_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_.SetRenderState(D3DRS_LIGHTING, FALSE);
        device_.SetRenderState(D3DRS_FOGENABLE, FALSE);
        device_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device_.SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device_.SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device_.SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device_.SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device_.SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        check(device_.SetTexture(0, texture), "SetTexture");
        struct Vertex { float x, y, z, u, v; };
        const Vertex quad[] = {{-1, 1, 0.5f, 0, 0}, {1, 1, 0.5f, 1, 0}, {-1, -1, 0.5f, 0, 1}, {1, -1, 0.5f, 1, 1}};
        device_.SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
        check(device_.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "DrawPrimitiveUP");
        Frame frame;
        // Device::draw records exactly the handle passed to bgfx::setTexture, including its readiness fallback.
        frame.selected = device_.last_draw.texture[0];
        check(device_.SetTexture(0, nullptr), "SetTexture(null)");
        check(device_.EndScene(), "EndScene");
        if (!noop_)
        {
            check(device_.GetRenderTargetData(target_, readback_), "GetRenderTargetData");
            D3DLOCKED_RECT locked{};
            check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
            frame.pixels.resize(kSize * kSize * 4);
            for (uint32_t y = 0; y < kSize; ++y)
                std::memcpy(frame.pixels.data() + y * kSize * 4,
                            static_cast<const uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
            check(readback_->UnlockRect(), "UnlockRect(readback)");
        }
        check(device_.Present(), "Present");
        return frame;
    }

  private:
    shim::Device &device_;
    bool noop_;
    shim::Surface *target_ = nullptr, *readback_ = nullptr;
};

void expect(const Frame &frame, uint16_t handle, const Pixels &expected, unsigned tolerance, const char *label)
{
    require(frame.selected == handle, label);
    require(frame.pixels.size() == expected.size(), "readback size");
    unsigned worst = 0;
    size_t visible = 0;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        worst = std::max(worst, unsigned(std::abs(int(frame.pixels[i]) - int(expected[i]))));
        if (i % 4 != 3 && frame.pixels[i] > 16)
            ++visible;
    }
    if (!expected.empty())
        require(visible >= kSize * kSize && worst <= tolerance, label);
    std::printf("PASS: %s (selected %u%s", label, frame.selected, expected.empty() ? ", submission only" : ", pixels");
    if (!expected.empty())
        std::printf(" worst difference %u/255", worst);
    std::puts(")");
}

void stage_upload(effects::UpscaleEntry &entry, texture::GpuFormat format, uint32_t size)
{
    const Pixels rgba = pattern(size, true);
    const texture::Blob ktx = texture::encode_rgba_to_ktx(rgba.data(), size, size, format);
    require(ktx.data && ktx.size, "encode fixture KTX");
    entry.ktx.assign(ktx.data.get(), ktx.data.get() + ktx.size);
    entry.state = effects::UpscaleEntry::Read;
}

void run(shim::Device &device, bool noop)
{
    Harness harness(device, noop);
    auto *base = make_texture(device, kSize, false);
    auto *reference = make_texture(device, kSize * 2, true);
    // Let the initial preset/self-test finish before controlling upscale readiness. A late preset commit would
    // legitimately disable the option and invalidate the seeded upload states halfway through this fixture.
    if (!noop)
    {
        const auto prepared_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        unsigned frames = 0;
        do { harness.draw(base); }
        while (render::CurrentEffects().pending && ++frames < 240 && std::chrono::steady_clock::now() < prepared_by);
        require(!render::CurrentEffects().pending, "initial effects preset settled");
    }
    const Frame original = harness.draw(base);
    const Frame replacement = harness.draw(reference);
    require(original.selected == base->store->handle.idx, "reference uses actual base binding");
    if (!noop)
    {
        require(original.pixels != replacement.pixels, "patterns distinguish base from replacement");
        expect(original, base->store->handle.idx, original.pixels, 0, "base reference is visible");
    }
    auto &upscales = effects::upscales();
    upscales.configure(true, 16);
    require(upscales.active(), "upscale supported and enabled");
    upscales.on_texture_loaded(*base->store, "locations/stone_swap", "fixture/stone_swap.tga.tx");
    require(bool(base->store->upscale), "real Upscales registration creates entry");
    auto entry = base->store->upscale;
    const uint16_t base_handle = base->store->handle.idx;
    int owner = 0;
    render::SetEffectsWorldOrigin({10000.0f, 0.0f, 0.0f});
    render::SetEffectsPlayerPosition({10003.0f, 0.0f, 0.0f}, &owner);
    base->store->normal_map = std::make_shared<effects::NormalMapEntry>();
    base->store->normal_map->state = effects::NormalMapEntry::Waiting;
    entry->state = effects::UpscaleEntry::Waiting;
    expect(harness.draw(base), base_handle, original.pixels, 0, "pending draw records player proximity without changing pixels");
    const auto observed = effects::generation_focus().snapshot();
    const float normal_distance = base->store->normal_map->priority.distance(observed);
    const float upscale_distance = entry->priority.distance(observed);
    require(normal_distance > 1.5f && normal_distance < 2.0f && std::abs(normal_distance-upscale_distance) < 0.01f,
            "actual draw bounds publish the same player-relative distance to both generation lanes");
    std::printf("PASS: actual player-distance publication %.2f metres to normal and upscale entries\n", normal_distance);
    base->store->normal_map.reset();
    render::ClearEffectsPlayerPosition(&owner);
    render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});

    for (const auto state : {effects::UpscaleEntry::Pending, effects::UpscaleEntry::Waiting,
                            effects::UpscaleEntry::Missing, effects::UpscaleEntry::Failed})
    {
        entry->state = state;
        expect(harness.draw(base), base_handle, original.pixels, 0, "unfinished/failed upscale retains patterned base");
    }
    entry->state = effects::UpscaleEntry::Resident;
    require(!bgfx::isValid(entry->handle), "invalid resident fixture has no GPU allocation");
    expect(harness.draw(base), base_handle, original.pixels, 0, "invalid replacement handle retains base");

    const auto *caps = shim::Context::instance().caps();
    const bool bc = (caps->formats[bgfx::TextureFormat::BC1] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;
    const auto format = bc ? texture::GpuFormat::Bc1 : texture::GpuFormat::Etc2Rgb8;
    stage_upload(*entry, format, kSize); // valid container/GPU texture, deliberately not twice the base dimensions
    expect(harness.draw(base), base_handle, original.pixels, 0, "Read before rejected upload retains base");
    require(entry->state == effects::UpscaleEntry::Failed, "wrong-dimension upload is rejected by end_frame");
    expect(harness.draw(base), base_handle, original.pixels, 0, "rejected GPU replacement retains base");

    stage_upload(*entry, format, kSize * 2);
    expect(harness.draw(base), base_handle, original.pixels, 0, "Read before successful upload retains base");
    require(entry->state == effects::UpscaleEntry::Resident && bgfx::isValid(entry->handle), "real upload becomes Resident");
    const uint16_t upscale_handle = entry->handle.idx;
    require(upscale_handle != base_handle, "replacement allocation differs from retained base");
    for (int i = 0; i < 3; ++i)
        expect(harness.draw(base), upscale_handle, replacement.pixels, 10, "resident replacement draws without blank frame");
    require(base->store->handle.idx == base_handle, "resident upscale keeps original base allocation");

    upscales.configure(false, 16);
    require(!bgfx::isValid(entry->handle), "disable releases upscale allocation");
    expect(harness.draw(base), base_handle, original.pixels, 0, "disabled upscale immediately returns to base");
    upscales.configure(true, 16);
    // A cancellation/reset state is seeded explicitly: this does not claim to test inference cancellation.
    entry->state = effects::UpscaleEntry::Missing;
    expect(harness.draw(base), base_handle, original.pixels, 0, "cancelled-work equivalent retains base");
    stage_upload(*entry, format, kSize * 2);
    expect(harness.draw(base), base_handle, original.pixels, 0, "second upload retains base before readiness");
    require(entry->state == effects::UpscaleEntry::Resident, "second real upload becomes resident");
    // Wait only for the production 500 ms residency plan; every intervening frame must retain valid imagery.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (entry->state == effects::UpscaleEntry::Resident && std::chrono::steady_clock::now() < until)
    {
        const uint16_t selected = entry->handle.idx;
        expect(harness.draw(base, 1000.0f), selected, replacement.pixels, 10, "far surface remains visible until eviction");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(entry->state != effects::UpscaleEntry::Resident && !bgfx::isValid(entry->handle), "real residency plan evicts far upscale");
    expect(harness.draw(base, 1000.0f), base_handle, original.pixels, 0, "eviction returns to patterned base without blank frame");
    upscales.configure(false, 16);
    base->store->upscale.reset();
    entry.reset();
    base->Release();
    reference->Release();
}
} // namespace

int main(int argc, char **argv)
{
    const bool noop = argc == 2 && std::strcmp(argv[1], "--noop") == 0;
    require(argc == 1 || noop, "usage: upscale_swap_check [--noop]");
    SDL_Window *window = nullptr;
    if (noop)
        require(shim::Context::instance().init_headless(kSize, kSize), "headless Noop start");
    else
    {
        require(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video start (X display required)");
        host_check::UseBackendFromEnvironment();
        window = SDL_CreateWindow("upscale_swap_check", 0, 0, kSize, kSize, 0);
        require(window && shim::Context::instance().init(window, kSize, kSize, false), "graphics backend start");
    }
    const auto directory = std::filesystem::temp_directory_path() /
        ("storm_upscale_swap_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    effects::ConfigureUpscaleDirectory(directory);
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = params.BackBufferHeight = kSize;
        auto *device = new shim::Device(window, params);
        run(*device, noop);
        device->Release();
    }
    if (window)
        SDL_DestroyWindow(window);
    if (!noop)
        SDL_Quit();
    std::filesystem::remove_all(directory);
    std::puts(noop ? "PASS: actual draw binding and upscale state transitions on Noop; pixels not checked"
                   : "PASS: actual draw binding and visible base/replacement pixels across upscale transitions");
    return 0;
}
