// A material whose texture alpha cuts holes casts with those holes in every pass that draws depth instead of colour
// (src/effects/casters.hpp), on OpenGL ES in an SDL window (an X display, like ao_volume_check). A 2 x 2 m plate with a
// checkerboard alpha texture (0.5 m squares) hangs 1 m over a 6 x 6 m floor seen from straight above:
//   * the lamp atlas of the still geometry: the plate is reported with the location's geometry (not drawn);
//   * the lamp atlas of the characters: the plate is drawn as a lit, alpha-tested character;
//   * the sun cascade: the plate is drawn lit and alpha-tested under a slanting sun.
// In each the floor behind an opaque square is in shadow and the floor behind a hole next to it is lit; with an opaque
// texture both are in shadow. A blended plate that writes no depth (glass) casts nothing.
#include "device.hpp"
#include "check_backend.hpp"

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
    float uv[2];
};

// How the plate takes part in a frame.
enum class Plate
{
    None,      // only reported with the geometry, or absent
    Character, // drawn lit with the characters' technique and the game's alpha test
    Lit,       // drawn lit (a prop) with the alpha test
    Glass,     // drawn blended without depth write
};

struct Scene
{
    bool lit_floor = false;        // the floor is a lit draw (the sun's receiver); else baked colours
    const D3DLIGHT9 *sun = nullptr; // the directional light of the lit draws
    Plate plate = Plate::None;
    shim::Texture2D *texture = nullptr;
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

    // The floor from 5 m above, orthographic over 6 x 6 m; returns the picture (BGRA).
    std::vector<uint8_t> render(const Scene &scene)
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
        render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        d_.SetTransform(D3DTS_WORLD, &identity);
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_AMBIENT, 0xFF404040);
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        D3DMATERIAL9 material{};
        material.Diffuse = material.Ambient = {1.0f, 1.0f, 1.0f, 1.0f};
        d_.SetMaterial(&material);
        if (scene.sun)
        {
            d_.SetLight(0, scene.sun);
            d_.LightEnable(0, TRUE);
        }
        else
            d_.LightEnable(0, FALSE);
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

        // The floor: 12 x 12 quads, no texture.
        d_.SetRenderState(D3DRS_LIGHTING, scene.lit_floor ? TRUE : FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        std::vector<Vertex> floor;
        const int n = 12;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
            {
                const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                for (const auto &c : corners)
                    floor.push_back({{-3.0f + 6.0f * (x + c[0]) / n, 0.0f, -3.0f + 6.0f * (z + c[1]) / n},
                                     {0.0f, 1.0f, 0.0f},
                                     0xFF909090u,
                                     {0.0f, 0.0f}});
            }
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(floor.size() / 3), floor.data(), sizeof(Vertex)),
              "draw floor");

        // The plate: its texture at stage 0, the alpha of the texture tested as the game tests it.
        if (scene.plate != Plate::None)
        {
            d_.SetRenderState(D3DRS_LIGHTING, TRUE);
            d_.SetTexture(0, scene.texture);
            d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            d_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
            d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            d_.SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            d_.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
            if (scene.plate == Plate::Glass)
            {
                d_.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                d_.SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            }
            else
            {
                d_.SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                d_.SetRenderState(D3DRS_ALPHAREF, 0xa0);
                d_.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            }
            if (scene.plate == Plate::Character)
                render::SetEffectsTechnique("animation");
            const Vertex plate[6] = {
                {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f}},
                {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 0.0f}},
                {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 1.0f}},
                {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f}},
                {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 1.0f}},
                {{-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 1.0f}},
            };
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, plate, sizeof(Vertex)), "draw plate");
            render::SetEffectsTechnique(nullptr);
        }
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

// Mean green of a few pixels around the floor point (x, z) (the picture is read back top row first).
float floor_at(const std::vector<uint8_t> &p, float x, float z)
{
    const int cx = static_cast<int>((x + 3.0f) / 6.0f * kSize), cy = static_cast<int>((3.0f - z) / 6.0f * kSize);
    float sum = 0.0f;
    int count = 0;
    for (int y = cy - 1; y <= cy + 1; ++y)
        for (int xx = cx - 1; xx <= cx + 1; ++xx)
        {
            sum += p[(static_cast<size_t>(y) * kSize + xx) * 4 + 1];
            ++count;
        }
    return sum / static_cast<float>(count);
}

// A 64 x 64 A8R8G8B8 texture: white; with `checker` the alpha of every other 16-texel square is 0 (the square at
// u, v < 0.25 is opaque). Reported to the effects as a game file, so they know whether it cuts holes.
shim::Texture2D *make_texture(Device &device, bool checker, const char *name)
{
    shim::Texture2D *texture = nullptr;
    check(device.CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
    D3DLOCKED_RECT rect;
    check(texture->LockRect(0, &rect, nullptr, 0), "LockRect");
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const bool hole = checker && ((x / 16 + y / 16) & 1);
            const uint32_t texel = hole ? 0x00FFFFFFu : 0xFFFFFFFFu;
            std::memcpy(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4, &texel, 4);
        }
    texture->UnlockRect(0);
    render::SetEffectsTextureSource(texture, name, {}); // no game file: no relief here
    return texture;
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
    SDL_Window *window = SDL_CreateWindow("cutout_caster_check", 0, 0, kSize, kSize, 0);
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
        {
            Harness harness(*device);
            shim::Texture2D *checker = make_texture(*device, true, "cutout_caster_check\\checker.tga");
            shim::Texture2D *opaque = make_texture(*device, false, "cutout_caster_check\\opaque.tga");

            // The still geometry: the floor slab, and for the first pass the plate with its texture.
            std::vector<std::array<float, 3>> positions = {{-3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, 3.0f},
                                                           {-3.0f, 0.0f, 3.0f}, {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},
                                                           {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f}};
            const std::vector<std::array<float, 2>> uvs = {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f},
                                                           {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            const std::vector<uint32_t> colors(positions.size(), 0xFF909090u);
            const std::vector<uint32_t> with_plate = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
            const std::vector<uint32_t> floor_only = {0, 1, 2, 0, 2, 3};
            const std::array<float, 3> room_lo{-3.0f, -0.2f, -3.0f}, room_hi{3.0f, 3.5f, 3.0f};
            // The lamp 3 m over the middle: the plate's shadow on the floor is 1.5 times the plate. Behind the opaque
            // square at the plate's corner (-0.75, -0.75): floor (-1.125, -1.125); behind the hole next to it
            // (-0.75, -0.25): floor (-1.125, -0.375). Both lie outside the plate as seen from above.
            const render::EffectsPointLight lamp{{0.0f, 3.0f, 0.0f}, {4.0f, 3.6f, 3.0f}, 15.0f, 0.5f, 0.0f, 0.1f};
            // The sun from (1, 2, 0): the shadow lies 0.5 m towards -x. Behind the same opaque square: floor
            // (-1.25, -0.75); behind the hole: (-1.25, -0.25).
            D3DLIGHT9 sun{};
            sun.Type = D3DLIGHT_DIRECTIONAL;
            sun.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
            sun.Direction = {-1.0f, -2.0f, 0.0f};
            render::EffectsWeather weather;
            weather.sun_dir = {0.4472f, 0.8944f, 0.0f};
            weather.sky_sun_dir = weather.sun_dir;
            weather.sun_present = true;

            struct Case
            {
                const char *name;
                bool lamps;                 // the lamp shines (else the sun)
                bool reported;              // the plate is part of the reported still geometry
                Plate plate;
                shim::Texture2D *texture;
                std::array<float, 2> behind_square, behind_hole;
                int expect;                 // 0: holes (the hole lit, the square dark), 1: whole shadow, 2: no shadow
            };
            const Case cases[] = {
                {"lamp, still geometry, checkerboard", true, true, Plate::None, checker, {-1.125f, -1.125f}, {-1.125f, -0.375f}, 0},
                {"lamp, still geometry, opaque", true, true, Plate::None, opaque, {-1.125f, -1.125f}, {-1.125f, -0.375f}, 1},
                {"lamp, character, checkerboard", true, false, Plate::Character, checker, {-1.125f, -1.125f}, {-1.125f, -0.375f}, 0},
                {"lamp, character, opaque", true, false, Plate::Character, opaque, {-1.125f, -1.125f}, {-1.125f, -0.375f}, 1},
                {"lamp, glass (blended, no depth)", true, false, Plate::Glass, opaque, {-1.125f, -1.125f}, {-1.125f, -0.375f}, 2},
                {"sun, lit plate, checkerboard", false, false, Plate::Lit, checker, {-1.25f, -0.75f}, {-1.25f, -0.25f}, 0},
                {"sun, lit plate, opaque", false, false, Plate::Lit, opaque, {-1.25f, -0.75f}, {-1.25f, -0.25f}, 1},
                {"sun, glass (blended, no depth)", false, false, Plate::Glass, opaque, {-1.25f, -0.75f}, {-1.25f, -0.25f}, 2},
            };
            for (const Case &c : cases)
            {
                Scene scene;
                scene.lit_floor = !c.lamps;
                scene.sun = c.lamps ? nullptr : &sun;
                scene.plate = c.plate;
                scene.texture = c.texture;
                const render::EffectsLocationGeometry::Surface surface{6, 6, c.texture};
                render::SetEffectsLocationGeometry({"cutout_caster_check", positions, colors, uvs,
                                                    c.reported ? std::span<const uint32_t>(with_plate) : std::span<const uint32_t>(floor_only),
                                                    c.reported ? std::span(&surface, 1) : std::span<const render::EffectsLocationGeometry::Surface>(),
                                                    c.lamps, room_lo, room_hi});
                render::ConfigureEffects(2, {{"fx_ao_volume", 0}, {"fx_lamp_shadows", c.lamps ? 8 : 0}, {"fx_shadows", c.lamps ? 0 : 2}});
                std::vector<uint8_t> picture;
                for (int frame = 0; frame < 400; ++frame)
                {
                    if (c.lamps)
                        render::SetEffectsLamps(std::span(&lamp, 1));
                    else
                    {
                        render::SetEffectsLamps({});
                        render::SetEffectsWeather(weather);
                    }
                    picture = harness.render(scene);
                    if (!render::CurrentEffects().pending && frame > 40)
                        break;
                    SDL_Delay(5);
                }
                if (const char *dump = std::getenv("CUTOUT_CHECK_DUMP"))
                {
                    char file[512];
                    std::snprintf(file, sizeof(file), "%s/case_%d.ppm", dump, static_cast<int>(&c - cases));
                    if (FILE *f = std::fopen(file, "wb"))
                    {
                        std::fprintf(f, "P6 %u %u 255\n", kSize, kSize);
                        for (size_t i = 0; i < picture.size(); i += 4)
                            std::fputc(picture[i + 2], f), std::fputc(picture[i + 1], f), std::fputc(picture[i], f);
                        std::fclose(f);
                    }
                }
                const float square = floor_at(picture, c.behind_square[0], c.behind_square[1]);
                const float hole = floor_at(picture, c.behind_hole[0], c.behind_hole[1]);
                const float open = floor_at(picture, 2.4f, 2.4f); // far from the plate's shadow
                bool pass = !render::CurrentEffects().pending;
                if (c.expect == 0)
                    pass &= square < 0.85f * hole && hole > 0.9f * open;
                else if (c.expect == 1)
                    pass &= square < 0.85f * open && hole < 0.85f * open;
                else
                    pass &= square > 0.9f * open && hole > 0.9f * open && std::fabs(square - hole) < 0.05f * open;
                std::printf("%s %s: floor behind an opaque square %.1f, behind a hole %.1f, open %.1f\n", pass ? "PASS" : "FAIL",
                            c.name, square, hole, open);
                ok &= pass;
            }
            render::SetEffectsLamps({});
            render::SetEffectsLocationGeometry({});
            render::ConfigureEffects(0, {});
            harness.render({});
            checker->Release();
            opaque->Release();
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: materials that cut holes cast with their holes in the lamp atlases and the sun cascade; glass "
                     "casts nothing\n"
                   : "FAIL: cut-out casters\n");
    return ok ? 0 : 1;
}
