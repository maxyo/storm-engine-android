// The interior AO volume end to end (src/effects/ao_volume.hpp, ff_fragment.sh ffAoVolume) on OpenGL ES in an SDL
// window (an X display, like skinning_check): a 6 x 6 m floor with baked vertex colours (lighting off, as the
// location models draw) under a 1.2 m table top at 0.8 m, seen from above. The table top is part of the reported
// geometry, not drawn, so the floor under it stays in view.
//   «Низкая»: no volume, the floor is one colour;
//   «Высокая» with fx_ao_volume=0: the baked variant must reproduce exactly that colour;
//   fx_ao_volume=1..3: under the table clearly darker, the open floor unchanged, stronger levels darker.
#include "device.hpp"
#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
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

struct Vertex
{
    float pos[3];
    float nrm[3];
    uint32_t color;
};

void add_box(std::vector<std::array<float, 3>> &p, std::vector<uint32_t> &idx, std::array<float, 3> lo,
             std::array<float, 3> hi)
{
    const auto base = static_cast<uint32_t>(p.size());
    for (int c = 0; c < 8; ++c)
        p.push_back({c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]});
    const uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto &f : faces)
        for (const uint32_t t : {f[0], f[1], f[2], f[0], f[2], f[3]})
            idx.push_back(base + t);
}

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

    // The floor from 5 m above, orthographic over 6 x 6 m; returns the picture (BGRA).
    // `light`: a lit floor with this one light; none: the baked floor.
    std::vector<uint8_t> render(const D3DLIGHT9 *light = nullptr)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        // Row-vector view: world x -> view x, world z -> view y, world y -> towards the camera; eye 5 m up.
        D3DMATRIX view{};
        view.m[0][0] = 1.0f;
        view.m[1][2] = -1.0f;
        view.m[2][1] = 1.0f;
        view.m[3][2] = 5.0f;
        view.m[3][3] = 1.0f;
        D3DMATRIX projection{};
        projection.m[0][0] = projection.m[1][1] = 1.0f / 3.0f;
        projection.m[2][2] = 0.1f;
        projection.m[3][3] = 1.0f;
        D3DMATRIX world = identity;
        if (relative)
        {
            // The camera at (0, 5, 0) moves into the world matrix, as Renderer::SetCamera does.
            world.m[3][1] = -5.0f;
            view.m[3][2] = 0.0f;
            render::SetEffectsWorldOrigin({0.0f, 5.0f, 0.0f});
        }
        else
            render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        d_.SetTransform(D3DTS_WORLD, &world);
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, light ? TRUE : FALSE);
        d_.SetRenderState(D3DRS_AMBIENT, 0xFF202020);
        if (light)
        {
            D3DMATERIAL9 material{};
            material.Diffuse = material.Ambient = {1.0f, 1.0f, 1.0f, 1.0f};
            d_.SetMaterial(&material);
            D3DLIGHT9 shifted = *light; // the Renderer shifts the lights with the world matrices
            if (relative)
                shifted.Position.y -= 5.0f;
            d_.SetLight(0, &shifted);
            d_.LightEnable(0, TRUE);
        }
        // With a texture the floor is texture * colour: the game's point lights light a baked surface's texture.
        d_.SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, texture);
        // 12 x 12 quads: the interpolation of the baked colour is per vertex, the AO per pixel.
        std::vector<Vertex> floor;
        const int n = 12;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
            {
                const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                for (const auto &c : corners)
                    floor.push_back({{-3.0f + 6.0f * (x + c[0]) / n, 0.0f, -3.0f + 6.0f * (z + c[1]) / n},
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

    shim::Texture2D *texture = nullptr; // the floor's (white), null: none

  private:
    Device &d_;
    storm::shim::Surface *readback_ = nullptr, *back_ = nullptr;
};

// Mean green of a square of pixels around (u, v) in 0..1 picture coordinates.
float mean(const std::vector<uint8_t> &p, float u, float v)
{
    const int cx = static_cast<int>(u * kSize), cy = static_cast<int>(v * kSize);
    float sum = 0.0f;
    int count = 0;
    for (int y = cy - 3; y <= cy + 3; ++y)
        for (int x = cx - 3; x <= cx + 3; ++x)
        {
            sum += p[(static_cast<size_t>(y) * kSize + x) * 4 + 1];
            ++count;
        }
    return sum / static_cast<float>(count);
}

bool settle(Harness &harness, int preset, const std::vector<render::EffectsOption> &options)
{
    render::ConfigureEffects(preset, options);
    // The previous settings draw until the new ones are prepared; the volume bakes on a texture worker.
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
    SDL_Window *window = SDL_CreateWindow("ao_volume_check", 0, 0, kSize, kSize, 0);
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
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        auto *device = new Device(window, params);
        // Both ways the device can hold the scene: absolute, and camera-relative as the game's Renderer keeps it (no view
        // translation; world matrices shifted by minus the camera, render::SetEffectsWorldOrigin). The AO volume and
        // the lamps are absolute world data: the pictures must match.
        for (const bool relative : {false, true})
        {
            std::printf("%s\n", relative ? "camera-relative device (the game's Renderer):" : "absolute device:");
            Harness harness(*device);
            harness.relative = relative;
            std::vector<std::array<float, 3>> positions;
            std::vector<uint32_t> indices;
            add_box(positions, indices, {-3.0f, -0.05f, -3.0f}, {3.0f, 0.0f, 3.0f}); // the floor slab
            add_box(positions, indices, {-0.6f, 0.76f, -0.6f}, {0.6f, 0.8f, 0.6f});  // the table top
            // The room box one walks in: the floor with room above it.
            const std::array<float, 3> room_lo{-3.0f, -0.2f, -3.0f}, room_hi{3.0f, 3.0f, 3.0f};
            render::SetEffectsLocationGeometry({"ao_volume_check room", positions, {}, {}, indices, {}, true, room_lo, room_hi});

            // Picture coordinates: the centre is under the table, (0.2, 0.2) is open floor 1.8 m from the edge.
            settle(harness, 0, {});
            const auto low = harness.render();
            const float low_centre = mean(low, 0.5f, 0.5f), low_open = mean(low, 0.2f, 0.2f);
            std::printf("Низкая: under the table %.1f, open floor %.1f\n", low_centre, low_open);
            ok &= low_centre > 100.0f && std::fabs(low_centre - low_open) <= 1.0f;

            if (!settle(harness, 2, {{"fx_ao_volume", 0}}))
            {
                std::printf("FAIL: «Высокая» still pending\n");
                ok = false;
            }
            const auto off = harness.render();
            const float off_centre = mean(off, 0.5f, 0.5f);
            std::printf("Высокая, AO volume off: under the table %.1f\n", off_centre);
            ok &= std::fabs(off_centre - low_centre) <= 2.0f;

            float previous = 1e9f;
            for (int level = 1; level <= 3; ++level)
            {
                if (!settle(harness, 2, {{"fx_ao_volume", level}}))
                {
                    std::printf("FAIL: «Высокая», fx_ao_volume=%d still pending\n", level);
                    ok = false;
                }
                const auto on = harness.render();
                const float centre = mean(on, 0.5f, 0.5f), open = mean(on, 0.2f, 0.2f);
                // Level 1 follows the game's data (a 1.2 m table top at 0.8 m hides little of the sky), 3 is stylised.
                const float darker = 1.0f - 0.06f * static_cast<float>(level);
                const bool level_ok = centre < darker * open && std::fabs(open - low_open) <= 0.06f * low_open &&
                                      centre < previous;
                std::printf("%s fx_ao_volume=%d: under the table %.1f, open floor %.1f (%.0f %% of open)\n",
                            level_ok ? "PASS" : "FAIL", level, centre, open, 100.0f * centre / open);
                ok &= level_ok;
                previous = centre;
            }

            // The lamps relight the room with their shadows: a lamp 2 m above the floor at (-2, -2) and a 0.6 m pillar
            // between it and the far side. On the baked floor and on a lit floor alike, the floor behind the pillar
            // (seen from the lamp) is darker than the floor beside it at the same distance; without the lamps there
            // is no such shadow; and the lamp brightens the baked floor it reaches (half of its light is new).
            add_box(positions, indices, {-1.3f, 0.0f, -1.3f}, {-0.7f, 2.0f, -0.7f});
            const std::vector<uint32_t> baked_colors(positions.size(), 0xFF808080u);
            render::SetEffectsLocationGeometry(
                {"ao_volume_check room with a pillar", positions, baked_colors, {}, indices, {}, true, room_lo, room_hi});
            const render::EffectsPointLight lamp{{-2.0f, 2.0f, -2.0f}, {4.0f, 3.6f, 3.0f}, 15.0f, 0.5f, 0.0f, 0.1f};
            D3DLIGHT9 lamp_light{};
            lamp_light.Type = D3DLIGHT_POINT;
            lamp_light.Diffuse = {4.0f, 3.6f, 3.0f, 1.0f};
            lamp_light.Position = {-2.0f, 2.0f, -2.0f};
            lamp_light.Range = 15.0f;
            lamp_light.Attenuation0 = 0.5f;
            lamp_light.Attenuation2 = 0.1f;
            float baked_beside_without = 0.0f;
            for (const bool lit_floor : {false, true})
                for (const int count : {0, 8})
                {
                    const D3DLIGHT9 *light = lit_floor ? &lamp_light : nullptr;
                    const char *floor = lit_floor ? "lit" : "baked";
                    render::ConfigureEffects(2, {{"fx_ao_volume", 0}, {"fx_lamp_shadows", count}});
                    for (int frame = 0; frame < 400; ++frame)
                    {
                        render::SetEffectsLamps(std::span(&lamp, 1));
                        harness.render(light);
                        if (!render::CurrentEffects().pending && frame > 30)
                            break;
                        SDL_Delay(5);
                    }
                    const bool ready = !render::CurrentEffects().pending;
                    render::SetEffectsLamps(std::span(&lamp, 1));
                    const auto picture = harness.render(light);
                    if (const char *dump = std::getenv("AO_CHECK_DUMP"))
                    {
                        char name[512];
                        std::snprintf(name, sizeof(name), "%s/lamp_%s_%d.ppm", dump, floor, count);
                        if (FILE *f = std::fopen(name, "wb"))
                        {
                            std::fprintf(f, "P6 %u %u 255\n", kSize, kSize);
                            for (size_t i = 0; i < picture.size(); i += 4)
                                std::fputc(picture[i + 2], f), std::fputc(picture[i + 1], f), std::fputc(picture[i], f);
                            std::fclose(f);
                        }
                    }
                    // Picture (read back top row first): x = (world x + 3) / 6, y = (3 - world z) / 6. Behind the
                    // pillar from the lamp: world (0.5, 0.5); beside it, as far from the lamp: (1.2, -2.0).
                    const float behind = mean(picture, (0.5f + 3.0f) / 6.0f, (3.0f - 0.5f) / 6.0f);
                    const float beside = mean(picture, (1.2f + 3.0f) / 6.0f, (3.0f + 2.0f) / 6.0f);
                    std::printf("%s floor, fx_lamp_shadows=%d: behind the pillar %.1f, beside it %.1f\n", floor, count, behind,
                                beside);
                    if (!ready)
                    {
                        std::printf("FAIL: fx_lamp_shadows=%d still pending\n", count);
                        ok = false;
                    }
                    const bool shadow = behind < 0.85f * beside;
                    if (count == 0 && shadow)
                    {
                        std::printf("FAIL: a shadow behind the pillar without the lamps (%s floor)\n", floor);
                        ok = false;
                    }
                    if (count != 0 && !shadow)
                    {
                        std::printf("FAIL: no lamp shadow behind the pillar (%s floor)\n", floor);
                        ok = false;
                    }
                    if (!lit_floor && count == 0)
                        baked_beside_without = beside;
                    if (!lit_floor && count != 0 && (beside < 1.05f * baked_beside_without || behind > 0.97f * baked_beside_without))
                    {
                        std::printf("FAIL: the lamp must brighten the baked floor it reaches (%.1f -> %.1f) and darken its "
                                    "shadow (%.1f)\n",
                                    baked_beside_without, beside, behind);
                        ok = false;
                    }
                    if (lit_floor && count != 0 && beside < 100.0f)
                    {
                        std::printf("FAIL: the lit floor beside the pillar is dark (shadow acne or no lamp light)\n");
                        ok = false;
                    }
                }
            // A carried lantern (a light that moves, render::SetEffectsDynamicLights) with a lamp slot of its own: the
            // pillar's shadow lies on the far side of the pillar from wherever the lantern is, and follows it; without a
            // moving slot there is no shadow.
            render::SetEffectsLamps({});
            shim::Texture2D *white = nullptr;
            check(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &white, nullptr), "CreateTexture");
            D3DLOCKED_RECT rect;
            check(white->LockRect(0, &rect, nullptr, 0), "LockRect");
            for (int y = 0; y < 4; ++y)
                std::memset(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch, 0xFF, 16);
            white->UnlockRect(0);
            harness.texture = white;
            for (const int moving : {0, 1})
                for (const float lantern_x : {-2.0f, 2.0f})
                {
                    const render::EffectsPointLight lantern{{lantern_x, 2.0f, -2.0f}, {4.0f, 3.6f, 3.0f}, 15.0f, 0.5f, 0.0f, 0.1f};
                    render::ConfigureEffects(2, {{"fx_ao_volume", 0}, {"fx_lamp_shadows", 8}, {"fx_lamp_moving", moving}});
                    std::vector<uint8_t> picture;
                    for (int frame = 0; frame < 400; ++frame)
                    {
                        render::SetEffectsDynamicLights(std::span(&lantern, 1));
                        picture = harness.render();
                        // A light that appears somewhere else is another light: the slot fades out and in again.
                        if (!render::CurrentEffects().pending && frame > 150)
                            break;
                        SDL_Delay(5);
                    }
                    // The pillar stands at (-1, -1). From the lantern at (-2, 2, -2) its shadow covers the floor at
                    // (0.5, 0.5); from (2, 2, -2) the floor at (-2.5, -0.5). Each is lit from the other place.
                    const float from_left = mean(picture, (0.5f + 3.0f) / 6.0f, (3.0f - 0.5f) / 6.0f);
                    const float from_right = mean(picture, (-2.5f + 3.0f) / 6.0f, (3.0f + 0.5f) / 6.0f);
                    const float shadowed = lantern_x < 0.0f ? from_left : from_right;
                    const float lit = lantern_x < 0.0f ? from_right : from_left;
                    // With or without a slot it is the same added light (texture * light); the slot adds the shadow.
                    const bool pass = !render::CurrentEffects().pending && lit > 150.0f &&
                                      (moving ? shadowed < 0.8f * lit : shadowed > 0.9f * lit);
                    std::printf("%s lantern at x = %.0f, fx_lamp_moving=%d: floor behind the pillar %.1f, the other side's "
                                "spot %.1f\n",
                                pass ? "PASS" : "FAIL", lantern_x, moving, shadowed, lit);
                    ok &= pass;
                }
            render::SetEffectsDynamicLights({});
            // A ship's lantern: the same room is now a ship (its geometry in the ship's space, reported once when the
            // effects ask), the lantern stands at (-2, 2, -2) on it. The shadow of the pillar lies where the ship puts
            // it, and when the ship turns a quarter it turns with it, from the same faces: nothing is reported again.
            render::SetEffectsLocationGeometry({});
            render::ConfigureEffects(2, {{"fx_ao_volume", 0}, {"fx_lamp_shadows", 8}, {"fx_lamp_moving", 1}});
            int reports = 0;
            for (const float quarter : {0.0f, 1.0f})
            {
                const float c = quarter > 0.5f ? 0.0f : 1.0f, sn = quarter > 0.5f ? 1.0f : 0.0f;
                const auto turned = [&](float x, float y, float z) { return std::array<float, 3>{x * c + z * sn, y, -x * sn + z * c}; };
                render::EffectsShip ship;
                ship.id = 7;
                ship.signature = 1;
                ship.world = {c, 0.0f, -sn, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sn, 0.0f, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
                const render::EffectsShipLight lantern{{turned(-2.0f, 2.0f, -2.0f), {4.0f, 3.6f, 3.0f}, 15.0f, 0.5f, 0.0f, 0.1f},
                                                       ship.id,
                                                       {-2.0f, 2.0f, -2.0f}};
                std::vector<uint8_t> picture;
                for (int frame = 0; frame < 400; ++frame)
                {
                    render::SetEffectsShipLights(std::span(&ship, 1), std::span(&lantern, 1));
                    if (render::EffectsShipGeometryWanted(ship.id, ship.signature))
                    {
                        render::SetEffectsShipGeometry(ship.id, ship.signature, {positions, {}, indices, {}});
                        ++reports;
                    }
                    picture = harness.render();
                    if (!render::CurrentEffects().pending && frame > 150)
                        break;
                    SDL_Delay(5);
                }
                const std::array<float, 3> behind = turned(0.5f, 0.0f, 0.5f), open = turned(-2.5f, 0.0f, -0.5f);
                const float shadowed = mean(picture, (behind[0] + 3.0f) / 6.0f, (3.0f - behind[2]) / 6.0f);
                const float lit = mean(picture, (open[0] + 3.0f) / 6.0f, (3.0f - open[2]) / 6.0f);
                const bool pass = !render::CurrentEffects().pending && lit > 150.0f && shadowed < 0.8f * lit && reports == 1;
                std::printf("%s ship's lantern, the ship turned %.0f quarter: floor behind the pillar %.1f, the open spot %.1f, "
                            "the ship's geometry reported %d time(s)\n",
                            pass ? "PASS" : "FAIL", quarter, shadowed, lit, reports);
                ok &= pass;
            }
            render::SetEffectsShipLights({}, {});
            harness.texture = nullptr;
            white->Release();
            render::SetEffectsLamps({});
            render::SetEffectsLocationGeometry({});
            render::ConfigureEffects(0, {});
            render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: the AO volume darkens under the table and nowhere else; a lamp shadow falls behind the pillar\n"
                : "FAIL: AO volume / lamp shadow check\n");
    return ok ? 0 : 1;
}
