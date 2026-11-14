// Wet surfaces in the rain end to end (src/effects/lighting.cpp "wet surfaces", shaders/include/ff_wet.sh) on OpenGL ES
// in an SDL window (an X display, like ao_volume_check): a 6 x 6 m floor with baked vertex colours (lighting off, as
// the location models draw) seen from 5 m above, far from the world origin (5000, 0, -3000).
//   «Низкая» in the rain: the floor as without it;
//   «Высокая» without a weather frame (an interior) and with fx_wet_surfaces=0 in the rain: the same colour;
//   «Высокая» in a full rain: the floor clearly darker; the device absolute or camera-relative (the game's Renderer):
//   the same picture;
//   a roof over half of the floor (in the location's geometry, so in its visibility volume): the floor under it stays
//   dry once the volume is baked, the open half is wet;
//   a street lamp seen over the wet floor at night («Высокая», camera-relative, perspective, the lamp relight on): its
//   highlight in the wet gloss shows on the street below it, absent when dry, and is gone where a wall shadows the lamp
//   from the floor (the highlight takes the lamp's own light and shadow, ffLampAdd).
#include "check_backend.hpp"
#include "device.hpp"
#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kSize = 96;
constexpr float kX = 5000.0f, kZ = -3000.0f; // the floor's centre in absolute world

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

struct Vertex
{
    float pos[3];
    float nrm[3];
    uint32_t color;
};

class Harness
{
  public:
    explicit Harness(Device &device) : d_(device)
    {
        check(d_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Harness()
    {
        readback_->Release();
        back_->Release();
    }

    bool relative = false;
    float rain = -1.0f; // < 0: no weather frame (an interior)

    // The floor from 5 m above, orthographic over 6 x 6 m; returns the picture (BGRA).
    std::vector<uint8_t> render()
    {
        if (rain >= 0.0f)
        {
            render::EffectsWeather weather;
            weather.rain = rain;
            weather.overcast = rain;
            weather.fog_color = {0.45f, 0.5f, 0.55f};
            render::SetEffectsWeather(weather);
        }
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        // Row-vector view: world x -> view x, world z -> view y, world y -> towards the camera; eye 5 m above the
        // floor's centre.
        D3DMATRIX view{};
        view.m[0][0] = 1.0f;
        view.m[1][2] = -1.0f;
        view.m[2][1] = 1.0f;
        view.m[3][3] = 1.0f;
        D3DMATRIX projection{};
        projection.m[0][0] = projection.m[1][1] = 1.0f / 3.0f;
        projection.m[2][2] = 0.1f;
        projection.m[3][3] = 1.0f;
        D3DMATRIX world = identity;
        if (relative)
        {
            // The camera moves into the world matrix, as Renderer::SetCamera does.
            world.m[3][0] = -kX;
            world.m[3][1] = -5.0f;
            world.m[3][2] = -kZ;
            render::SetEffectsWorldOrigin({kX, 5.0f, kZ});
        }
        else
        {
            view.m[3][0] = -kX;
            view.m[3][1] = -kZ;
            view.m[3][2] = 5.0f;
            render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        }
        d_.SetTransform(D3DTS_WORLD, &world);
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        std::vector<Vertex> floor;
        const int n = 12;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
            {
                const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                for (const auto &c : corners)
                    floor.push_back({{kX - 3.0f + 6.0f * (x + c[0]) / n, 0.0f, kZ - 3.0f + 6.0f * (z + c[1]) / n},
                                     {0.0f, 1.0f, 0.0f},
                                     0xFF909090u});
            }
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(floor.size() / 3), floor.data(), sizeof(Vertex)),
              "draw floor");
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        std::vector<uint8_t> pixels(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    Device &d_;
    storm::shim::Surface *readback_ = nullptr, *back_ = nullptr;
};

// A 24 m street at night seen from 1.7 m, towards a lamp 3 m up and 9 m ahead; camera-relative like the game.
class Street
{
  public:
    explicit Street(Device &device) : d_(device)
    {
        check(d_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Street()
    {
        readback_->Release();
        back_->Release();
    }

    float rain = 1.0f;
    static constexpr float kEyeX = kX, kEyeY = 1.7f, kEyeZ = kZ - 6.0f;

    std::vector<uint8_t> render(const render::EffectsPointLight &lamp)
    {
        render::EffectsWeather weather;
        weather.rain = rain;
        weather.overcast = rain;
        weather.fog_color = {0.05f, 0.05f, 0.06f};
        weather.ambient = {0.1f, 0.1f, 0.1f};
        render::SetEffectsWeather(weather);
        render::SetEffectsLamps(std::span(&lamp, 1));
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        // Looking along +z, 12 degrees down; the camera position is in the world matrix (render origin = the eye).
        const float a = 12.0f * 3.14159265f / 180.0f;
        D3DMATRIX view{};
        view.m[0][0] = 1.0f;
        view.m[1][1] = std::cos(a);
        view.m[1][2] = -std::sin(a);
        view.m[2][1] = std::sin(a);
        view.m[2][2] = std::cos(a);
        view.m[3][3] = 1.0f;
        const float n = 0.1f, f = 200.0f, cot = 1.0f / std::tan(0.5f * 1.0f); // 57 degrees
        D3DMATRIX projection{};
        projection.m[0][0] = projection.m[1][1] = cot;
        projection.m[2][2] = f / (f - n);
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -n * f / (f - n);
        D3DMATRIX world{};
        world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1.0f;
        world.m[3][0] = -kEyeX;
        world.m[3][1] = -kEyeY;
        world.m[3][2] = -kEyeZ;
        render::SetEffectsWorldOrigin({kEyeX, kEyeY, kEyeZ});
        d_.SetTransform(D3DTS_WORLD, &world);
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        std::vector<Vertex> floor;
        const int cells = 24;
        for (int z = 0; z < cells; ++z)
            for (int x = 0; x < cells; ++x)
            {
                const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                for (const auto &c : corners)
                    floor.push_back({{kX - 8.0f + 16.0f * (x + c[0]) / cells, 0.0f, kZ - 4.0f + 24.0f * (z + c[1]) / cells},
                                     {0.0f, 1.0f, 0.0f},
                                     0xFF303030u});
            }
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(floor.size() / 3), floor.data(), sizeof(Vertex)),
              "draw street");
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        std::vector<uint8_t> pixels(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    Device &d_;
    storm::shim::Surface *readback_ = nullptr, *back_ = nullptr;
};

void dump(const std::vector<uint8_t> &picture, const char *name)
{
    const char *dir = std::getenv("WET_CHECK_DUMP");
    if (!dir)
        return;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    if (FILE *f = std::fopen(path, "wb"))
    {
        std::fprintf(f, "P6 %u %u 255\n", kSize, kSize);
        for (size_t i = 0; i < picture.size(); i += 4)
            std::fputc(picture[i + 2], f), std::fputc(picture[i + 1], f), std::fputc(picture[i], f);
        std::fclose(f);
    }
}

// The brightness the rain adds per pixel (green, wet minus dry, clamped at 0) and the extent of the pixels where it
// adds more than `threshold`: rows (along the view) and columns (across).
struct Streak
{
    float peak = 0.0f;
    int rows = 0, columns = 0;
};
Streak streak(const std::vector<uint8_t> &dry, const std::vector<uint8_t> &wet, float threshold)
{
    Streak s;
    int top = kSize, bottom = -1, left = kSize, right = -1;
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * kSize + x) * 4 + 1;
            const float added = static_cast<float>(wet[i]) - static_cast<float>(dry[i]);
            s.peak = std::max(s.peak, added);
            if (added > threshold)
            {
                top = std::min<int>(top, y), bottom = std::max<int>(bottom, y);
                left = std::min<int>(left, x), right = std::max<int>(right, x);
            }
        }
    s.rows = bottom >= top ? bottom - top + 1 : 0;
    s.columns = right >= left ? right - left + 1 : 0;
    return s;
}

// Mean green over the picture, and the mean absolute difference of two pictures.
float mean(const std::vector<uint8_t> &p)
{
    double sum = 0.0;
    for (size_t i = 1; i < p.size(); i += 4)
        sum += p[i];
    return static_cast<float>(sum / (p.size() / 4));
}

float difference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        if (i % 4 != 3)
            sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    return static_cast<float>(sum / (a.size() / 4 * 3));
}

// Mean green of the columns [first, last).
float mean_columns(const std::vector<uint8_t> &p, uint32_t first, uint32_t last)
{
    double sum = 0.0;
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = first; x < last; ++x)
            sum += p[(static_cast<size_t>(y) * kSize + x) * 4 + 1];
    return static_cast<float>(sum / (kSize * (last - first)));
}

bool settle(Harness &harness, int preset, const std::vector<render::EffectsOption> &options)
{
    render::ConfigureEffects(preset, options);
    for (int frame = 0; frame < 400; ++frame)
    {
        harness.render();
        if (!render::CurrentEffects().pending && frame > 30)
            return true;
        SDL_Delay(5);
    }
    return !render::CurrentEffects().pending;
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
    SDL_Window *window = SDL_CreateWindow("wet_surfaces_check", 0, 0, kSize, kSize, 0);
    if (!window || !shim::Context::instance().init(window, kSize, kSize, false))
    {
        std::fprintf(stderr, "FAIL: no OpenGL ES window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    const auto expect = [&ok](bool condition, const char *what) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
        ok &= condition;
    };
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        auto *device = new Device(window, params);
        std::vector<uint8_t> soaked[2];
        for (const bool relative : {false, true})
        {
            std::printf("%s\n", relative ? "camera-relative device (the game's Renderer):" : "absolute device:");
            Harness harness(*device);
            harness.relative = relative;
            harness.rain = 1.0f;
            settle(harness, 0, {});
            const float low = mean(harness.render());
            std::printf("Низкая in the rain: %.1f\n", low);
            expect(low > 120.0f, "Низкая: the floor's own colour");

            harness.rain = -1.0f;
            expect(settle(harness, 2, {}), "Высокая prepared");
            const float interior = mean(harness.render());
            std::printf("Высокая without the weather: %.1f\n", interior);
            expect(std::fabs(interior - low) <= 2.0f, "Высокая without a weather frame (interior): dry");

            harness.rain = 1.0f;
            harness.render(); // the first weather frame after the gap starts at the rain's level
            soaked[relative] = harness.render();
            const float wet = mean(soaked[relative]);
            std::printf("Высокая in a full rain: %.1f (%.0f %% of dry)\n", wet, 100.0f * wet / low);
            expect(wet < 0.85f * low && wet > 0.35f * low, "Высокая in a full rain: the floor darker, not black");

            settle(harness, 2, {{"fx_wet_surfaces", 0}});
            const float off = mean(harness.render());
            expect(std::fabs(off - low) <= 2.0f, "fx_wet_surfaces=0 in the rain: dry");
            render::ConfigureEffects(0, {});
            render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        }
        const float apart = difference(soaked[0], soaked[1]);
        std::printf("absolute vs camera-relative, soaked: mean difference %.2f\n", apart);
        expect(apart < 2.0f, "the same wet picture on both kinds of device");

        // A roof 2.5 m over the left half of the floor (x < kX), only in the location's geometry: the visibility volume
        // sees it, the picture does not. Under it the rain does not arrive.
        std::printf("a roof over half of the floor (Высокая, soaked):\n");
        {
            std::vector<std::array<float, 3>> positions;
            std::vector<uint32_t> indices;
            const auto box = [&](std::array<float, 3> lo, std::array<float, 3> hi) {
                const auto base = static_cast<uint32_t>(positions.size());
                for (int c = 0; c < 8; ++c)
                    positions.push_back({c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]});
                const uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
                for (const auto &face : faces)
                    for (const uint32_t t : {face[0], face[1], face[2], face[0], face[2], face[3]})
                        indices.push_back(base + t);
            };
            box({kX - 12.0f, -0.2f, kZ - 12.0f}, {kX + 12.0f, 0.0f, kZ + 12.0f});
            box({kX - 12.0f, 2.5f, kZ - 12.0f}, {kX, 2.7f, kZ + 12.0f});
            const std::vector<uint32_t> colors(positions.size(), 0xFF909090u);
            render::EffectsLocationGeometry geometry{"wet_surfaces_check roof", positions, colors, {}, indices, {}, false, {}, {}};
            geometry.collision_positions = positions;
            geometry.collision_indices = indices;
            render::SetEffectsLocationGeometry(geometry);
            Harness harness(*device);
            harness.relative = true;
            harness.rain = 1.0f;
            settle(harness, 0, {});
            const std::vector<uint8_t> dry = harness.render();
            settle(harness, 2, {{"fx_wet_force", 100}});
            // The volume bakes on its thread: until it is there the rain reaches everywhere (logged).
            float roofed = 0.0f, open = 0.0f, roofed_dry = mean_columns(dry, 4, 36), open_dry = mean_columns(dry, 60, 92);
            for (int frame = 0; frame < 1500; ++frame)
            {
                const std::vector<uint8_t> wet = harness.render();
                roofed = mean_columns(wet, 4, 36);
                open = mean_columns(wet, 60, 92);
                if (roofed > roofed_dry - 3.0f)
                    break;
                SDL_Delay(5);
            }
            std::printf("under the roof %.1f (dry %.1f), open %.1f (dry %.1f)\n", roofed, roofed_dry, open, open_dry);
            expect(roofed > roofed_dry - 3.0f, "under the roof the floor stays dry");
            expect(open < 0.85f * open_dry, "beside the roof the floor is wet");
            render::SetEffectsLocationGeometry({});
            render::ConfigureEffects(0, {});
            render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        }

        // The street lamp at night, «Высокая» (its highlights) with its lamp relight. The location's geometry: the street
        // slab, and optionally a wall between the lamp and the floor in front of the viewer.
        std::printf("a street lamp over the wet street (Высокая, camera-relative):\n");
        {
            Street street(*device);
            const render::EffectsPointLight lamp{{kX, 3.0f, kZ + 9.0f}, {1.0f, 0.8f, 0.5f}, 14.0f, 0.3f, 0.0f, 0.02f};
            std::vector<uint8_t> pictures[2][2]; // [wall][wet]
            for (const bool wall : {false, true})
            {
                std::vector<std::array<float, 3>> positions;
                std::vector<uint32_t> indices;
                const auto box = [&](std::array<float, 3> lo, std::array<float, 3> hi) {
                    const auto base = static_cast<uint32_t>(positions.size());
                    for (int c = 0; c < 8; ++c)
                        positions.push_back({c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]});
                    const uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
                    for (const auto &face : faces)
                        for (const uint32_t t : {face[0], face[1], face[2], face[0], face[2], face[3]})
                            indices.push_back(base + t);
                };
                box({kX - 8.0f, -0.2f, kZ - 4.0f}, {kX + 8.0f, 0.0f, kZ + 20.0f});
                // A low wall across the street 1 m in front of the lamp: it hides the lamp from the floor where its
                // reflection would be seen (the street before the wall), not from the camera.
                if (wall)
                    box({kX - 8.0f, 0.0f, kZ + 7.6f}, {kX + 8.0f, 2.6f, kZ + 7.8f});
                const std::vector<uint32_t> colors(positions.size(), 0xFF303030u);
                render::SetEffectsLocationGeometry({"wet_surfaces_check street", positions, colors, {}, indices, {}, false, {}, {}});
                for (const bool wet : {false, true})
                {
                    // It rains in both; fx_wet_force holds the street dry or soaked (the rise takes 45 s otherwise).
                    street.rain = 1.0f;
                    render::ConfigureEffects(2, {{"fx_light_normal_map", 0}, {"fx_wet_force", wet ? 100 : 0}});
                    for (int frame = 0; frame < 400; ++frame)
                    {
                        street.render(lamp);
                        if (!render::CurrentEffects().pending && frame > 30)
                            break;
                        SDL_Delay(5);
                    }
                    pictures[wall][wet] = street.render(lamp);
                    char name[64];
                    std::snprintf(name, sizeof(name), "street_%s_%s", wall ? "wall" : "open", wet ? "wet" : "dry");
                    dump(pictures[wall][wet], name);
                }
            }
            const Streak open = streak(pictures[0][0], pictures[0][1], 12.0f);
            const Streak walled = streak(pictures[1][0], pictures[1][1], 12.0f);
            std::printf("open street: the rain adds up to %.0f, over %d rows x %d columns; behind the wall up to %.0f\n",
                        open.peak, open.rows, open.columns, walled.peak);
            expect(open.peak > 40.0f, "the wet street shows the lamp's highlight");
            expect(walled.peak < 0.5f * open.peak, "no reflection where the wall shadows the lamp (its own shadow)");
            render::SetEffectsLamps({});
            render::SetEffectsLocationGeometry({});
            render::ConfigureEffects(0, {});
            render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: wet surfaces darken in the rain only, dry under a roof, with the lamps' shadowed highlights\n"
                   : "FAIL: wet surfaces check\n");
    return ok ? 0 : 1;
}
