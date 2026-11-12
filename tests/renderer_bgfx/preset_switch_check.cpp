// Live graphics preset switch (effects::Host::advance_switch): the previous settings keep drawing until the new
// ones are prepared, the switch frame links no program, and no frame after it loses the processed scene.
//
// Renders a lit 3D quad into the game picture, marks the end of the scene (EffectsSceneComplete: the post chain
// runs there) and draws an interface rectangle over the processed picture, like every game frame; the picture is
// read back before each Present. Through OpenGL ES in an SDL window (an X display: xvfb-run in CI).
//   * «Низкая» -> «Высокая»: frames stay identical to «Низкая» while EffectsReport::pending, the frame after the
//     switch creates no bgfx program, and the first frames of «Высокая» show the scene (the post target written
//     by the chain was once cleared by the interface's first view: a black frame after every change).
//   * three interfaces in a frame (each ends the scene, as each game interface does): the post chain runs once.
//   * back to «Низкая»: the picture equals the first one; «Высокая» again: active at once (programs kept, the
//     self-test of these settings already passed).
#include "device.hpp"
#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kWidth = 128, kHeight = 96;
constexpr uint32_t kUiWidth = 32, kUiHeight = 24; // the interface rectangle, top left
constexpr int kMaxSwitchFrames = 120;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

struct SceneVertex
{
    float x, y, z;
    float nx, ny, nz;
    uint32_t color;
};

struct UiVertex
{
    float x, y, z, rhw;
    uint32_t color;
};

using Picture = std::vector<uint8_t>; // RGBA rows, kWidth x kHeight

class Frames
{
  public:
    explicit Frames(Device &device) : d_(device)
    {
        check(d_.CreateOffscreenPlainSurface(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Frames()
    {
        readback_->Release();
        back_->Release();
    }

    // One game frame: the 3D scene, the scene boundary, the interface; the game picture read back, then presented.
    // Every interface marks the end of the scene (as each game interface calls Renderer::MakePostProcess).
    Picture render(int interfaces = 1)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF2050A0, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        D3DMATRIX world = identity;
        world.m[3][2] = 5.0f;
        // D3D left-handed perspective, 90 degrees vertically, near 1, far 100.
        D3DMATRIX projection{};
        projection.m[0][0] = static_cast<float>(kHeight) / kWidth;
        projection.m[1][1] = 1.0f;
        projection.m[2][2] = 100.0f / 99.0f;
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -100.0f / 99.0f;
        d_.SetTransform(D3DTS_WORLD, &world);
        d_.SetTransform(D3DTS_VIEW, &identity);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, TRUE);
        d_.SetRenderState(D3DRS_AMBIENT, 0xFF303030);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        D3DLIGHT9 light{};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse = {0.9f, 0.85f, 0.8f, 1};
        light.Direction = {0.2f, -0.3f, 1.0f};
        d_.SetLight(0, &light);
        d_.LightEnable(0, TRUE);
        // A quad over most of the picture, tilted so that its lighting varies across it.
        const SceneVertex quad[6] = {
            {-6, -6, 1, 0.3f, 0.2f, -1, 0xFFC08040}, {6, -6, -1, -0.3f, 0.2f, -1, 0xFF40C080},
            {-6, 6, 1, 0.3f, -0.2f, -1, 0xFF8040C0}, {-6, 6, 1, 0.3f, -0.2f, -1, 0xFF8040C0},
            {6, -6, -1, -0.3f, 0.2f, -1, 0xFF40C080}, {6, 6, -1, -0.3f, -0.2f, -1, 0xFFC0C040},
        };
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(SceneVertex)), "draw (scene)");
        // The interfaces over the processed picture: pre-transformed, unlit.
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        const float w = kUiWidth, h = kUiHeight;
        const UiVertex ui[6] = {{0, 0, 0, 1, 0xFFE0E0E0}, {w, 0, 0, 1, 0xFFE0E0E0}, {0, h, 0, 1, 0xFFE0E0E0},
                                {0, h, 0, 1, 0xFFE0E0E0}, {w, 0, 0, 1, 0xFFE0E0E0}, {w, h, 0, 1, 0xFFE0E0E0}};
        d_.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        for (int i = 0; i < interfaces; ++i)
        {
            render::EffectsSceneComplete();
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, ui, sizeof(UiVertex)), "draw (interface)");
        }
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        Picture pixels(kWidth * kHeight * 4);
        for (uint32_t y = 0; y < kHeight; ++y)
            std::memcpy(&pixels[y * kWidth * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kWidth * 4);
        readback_->UnlockRect();
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    Device &d_;
    storm::shim::Surface *readback_ = nullptr, *back_ = nullptr;
};

// Mean of the colour channels over the picture outside the interface rectangle (the scene), 0..255.
double scene_mean(const Picture &picture)
{
    double sum = 0.0;
    uint32_t count = 0;
    for (uint32_t y = 0; y < kHeight; ++y)
        for (uint32_t x = 0; x < kWidth; ++x)
            if (x >= kUiWidth + 4 || y >= kUiHeight + 4)
            {
                const uint8_t *p = &picture[(y * kWidth + x) * 4];
                sum += (p[0] + p[1] + p[2]) / 3.0;
                ++count;
            }
    return sum / count;
}

uint32_t max_difference(const Picture &a, const Picture &b)
{
    uint32_t worst = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            worst = std::max<uint32_t>(worst, std::abs(int(a[i + c]) - int(b[i + c])));
    return worst;
}

uint32_t programs()
{
    return bgfx::getStats()->numPrograms;
}

bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

// Frames until the requested settings draw; every frame before must equal `previous` (the old settings draw).
int frames_until_active(Frames &frames, const Picture &previous, bool &kept_previous)
{
    kept_previous = true;
    for (int frame = 0; frame < kMaxSwitchFrames; ++frame)
    {
        if (!render::CurrentEffects().pending)
            return frame;
        const Picture picture = frames.render();
        if (max_difference(picture, previous) != 0)
            kept_previous = false;
    }
    return kMaxSwitchFrames;
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL video: %s (an X display is needed)\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("preset_switch_check", 0, 0, kWidth, kHeight, 0);
    if (!window || !shim::Context::instance().init(window, kWidth, kHeight, false))
    {
        std::fprintf(stderr, "FAIL: no OpenGL ES window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        auto *device = new Device(window, params);
        {
            Frames frames(*device);
            render::ConfigureEffects(0, {});
            Picture low;
            for (int i = 0; i < 3; ++i)
                low = frames.render();
            const double low_mean = scene_mean(low);
            ok &= expect(low_mean > 20.0, "«Низкая»: the scene is drawn");

            ok &= expect(render::ConfigureEffects(2, {}).pending, "«Высокая» requested: pending");
            bool kept = false;
            const int prepared = frames_until_active(frames, low, kept);
            std::printf("     «Высокая» active after %d frames\n", prepared);
            ok &= expect(prepared > 0 && prepared < kMaxSwitchFrames, "«Высокая» prepared over frames, then active");
            ok &= expect(kept, "«Низкая» keeps drawing while «Высокая» is prepared");
            const uint32_t before = programs();
            for (int i = 0; i < 3; ++i)
            {
                const Picture high = frames.render();
                const double mean = scene_mean(high);
                std::printf("     «Высокая» frame %d: scene mean %.1f («Низкая» %.1f)\n", i, mean, low_mean);
                ok &= expect(std::fabs(mean - low_mean) < low_mean * 0.35, "«Высокая»: the processed scene is shown");
            }
            ok &= expect(programs() == before, "the first frames of «Высокая» link no program");
            // Three interfaces end the scene three times: the post chain still runs once (the same picture as one).
            const Picture one = frames.render(1);
            const Picture three = frames.render(3);
            std::printf("     «Высокая», three interfaces: scene mean %.1f (one interface %.1f)\n", scene_mean(three),
                        scene_mean(one));
            ok &= expect(max_difference(one, three) <= 1, "«Высокая»: the post chain runs once per frame, whatever the interfaces");

            render::ConfigureEffects(0, {});
            for (int i = 0; i < kMaxSwitchFrames && render::CurrentEffects().pending; ++i)
                frames.render();
            const Picture last = frames.render();
            ok &= expect(max_difference(last, low) <= 1, "back to «Низкая»: the same picture as before the switch");

            render::ConfigureEffects(2, {});
            const int again = frames_until_active(frames, last, kept);
            ok &= expect(again == 1, "«Высокая» again: active at the next Present (programs kept, self-test passed)");
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: preset_switch_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
