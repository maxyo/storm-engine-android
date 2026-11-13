// The sea's soft edges (fs_port_sea2.sc with fx::SeaDepth, «Высокая») at the edges of objects in front of the sea,
// through the shim on OpenGL ES in an SDL window (an X display, like skinning_check).
//
// The game's Sea2 port draws a calm sea below the camera; an opaque post stands in front of it and ends above the
// water, so no water lies in front of anything: the soft edges have nothing to fade. «Высокая» with soft edges must
// then draw exactly the picture «Высокая» draws without them. The capture once held the post's distance for the
// water pixels next to it (a coarser capture; a multisampled edge pixel) and faded them out: bright fringes along
// every rope, sail and hull over the sea on the phone.
#include "device.hpp"
#include "ports.hpp"
#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kWidth = 160, kHeight = 96;
constexpr int kMaxSwitchFrames = 200;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

struct SeaVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct PostVertex
{
    float x, y, z;
    uint32_t color;
};

using Picture = std::vector<uint8_t>;

// The reference blob CTechnique hands the device for a ported technique shader (ports.hpp).
std::vector<DWORD> shader_ref(const std::string &key)
{
    std::vector<DWORD> words{shim::kShaderRefMagic};
    std::string padded = key;
    padded.resize((key.size() / 4 + 1) * 4, '\0');
    for (size_t i = 0; i < padded.size(); i += 4)
    {
        DWORD word = 0;
        std::memcpy(&word, padded.data() + i, 4);
        words.push_back(word);
    }
    words.push_back(0x0000FFFF);
    return words;
}

class Scene
{
  public:
    explicit Scene(Device &device) : d_(device)
    {
        check(d_.CreateOffscreenPlainSurface(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
        const std::vector<DWORD> vs = shader_ref("sea_sea2_vertex_shader"), ps = shader_ref("sea_sea2_pixel_shader");
        check(d_.CreateVertexShader(vs.data(), &vs_), "CreateVertexShader(sea2)");
        check(d_.CreatePixelShader(ps.data(), &ps_), "CreatePixelShader(sea2)");
        // The bump volume: the flat normal (0, 1, 0) after _bx2 everywhere.
        check(d_.CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &bump_, nullptr), "CreateVolumeTexture");
        D3DLOCKED_BOX box;
        check(bump_->LockBox(0, &box, nullptr, 0), "LockBox");
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(box.pBits) + z * box.SlicePitch + y * box.RowPitch + x * 4) =
                        0x8080FF80;
        bump_->UnlockBox(0);
        // The reflected scene: one sky colour.
        check(d_.CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &env_, nullptr), "CreateCubeTexture");
        for (int face = 0; face < 6; ++face)
        {
            D3DLOCKED_RECT rect;
            check(env_->LockRect(static_cast<D3DCUBEMAP_FACES>(face), 0, &rect, nullptr, 0), "LockRect(cube)");
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4) = 0xFF6080A0;
            env_->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), 0);
        }
    }
    ~Scene()
    {
        readback_->Release();
        back_->Release();
        vs_->Release();
        ps_->Release();
        bump_->Release();
        env_->Release();
    }

    Picture render()
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget");
        // Magenta under the sea: what a faded water pixel would show.
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFF00FF, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        // D3D left-handed perspective, 90 degrees vertically, near 0.5, far 500.
        D3DMATRIX projection{};
        const float n = 0.5f, f = 500.0f;
        projection.m[0][0] = static_cast<float>(kHeight) / kWidth;
        projection.m[1][1] = 1.0f;
        projection.m[2][2] = f / (f - n);
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -n * f / (f - n);
        d_.SetTransform(D3DTS_WORLD, &identity);
        d_.SetTransform(D3DTS_VIEW, &identity);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        // A post 6 m ahead, 0.6 m wide, from 0.5 m above the water up out of the picture: thin dark edges over the sea.
        const uint32_t grey = 0xFF404040;
        const PostVertex post[6] = {{-0.3f, -2.5f, 6}, {0.3f, -2.5f, 6}, {-0.3f, 20, 6},
                                    {-0.3f, 20, 6},    {0.3f, -2.5f, 6}, {0.3f, 20, 6}};
        PostVertex coloured[6];
        for (int i = 0; i < 6; ++i)
            coloured[i] = {post[i].x, post[i].y, post[i].z, grey};
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, coloured, sizeof(PostVertex)), "draw (post)");

        // The sea: the Sea2 technique (src/techniques/weather/sea.fx) with the constants SEA::Realize sets.
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
        d_.SetVertexShader(vs_);
        d_.SetPixelShader(ps_);
        d_.SetTexture(0, bump_);
        d_.SetTexture(3, env_);
        float c[59][4] = {};
        const auto set = [&](int r, float x, float y, float z, float w) {
            c[r][0] = x, c[r][1] = y, c[r][2] = z, c[r][3] = w;
        };
        set(0, 0.0f, 1.0f, 0.5f, -0.04f);
        set(1, 2.0f, -1.0f, 0.00036621652552071f, 0.0f); // no fog
        set(2, 0.0f, 0.0f, 0.0f, 0.0f);
        set(23, 0.0f, 0.0f, 0.0f, 1.0f); // camera
        // World-view-projection as the vertex shader reads it (m4x4 with c24..c27: the columns of the D3D matrix).
        for (int r = 0; r < 4; ++r)
            set(24 + r, projection.m[0][r], projection.m[1][r], projection.m[2][r], projection.m[3][r]);
        set(28, 0.2f, 0.7f, 0.6f, 0.0f);  // Sea2 attenuation, reflection, transparency
        set(29, 0.04f, 0.22f, 0.39f, 1.0f); // water colour
        set(30, 1.0f, 1.0f, 1.0f, 1.0f);  // sky colour
        set(33, 1.0f, 0.0f, 0.0f, 1.0f);
        set(34, 0.5f, 1.0f, 0.5f, 1.0f); // Frenel
        set(36, 1.0f, 0.0f, 0.0f, 0.0f);
        set(37, 0.0f, 1.0f, 0.0f, 0.0f);
        set(38, 0.0f, 0.0f, 1.0f, 0.0f);
        set(58, 0.0f, 1.0f, 0.0f, 1.0f);
        d_.SetVertexShaderConstantF(0, &c[0][0], 59);
        // A calm sea 3 m below the camera, from 1 m ahead to the far plane.
        const float y = -3.0f;
        const SeaVertex sea[6] = {{-400, y, 1, 0, 1, 0, 0, 0},   {400, y, 1, 0, 1, 0, 1, 0},
                                  {-400, y, 450, 0, 1, 0, 0, 1}, {-400, y, 450, 0, 1, 0, 0, 1},
                                  {400, y, 1, 0, 1, 0, 1, 0},    {400, y, 450, 0, 1, 0, 1, 1}};
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, sea, sizeof(SeaVertex)), "draw (sea)");
        d_.SetVertexShader(nullptr);
        d_.SetPixelShader(nullptr);
        d_.SetTexture(0, nullptr);
        d_.SetTexture(3, nullptr);
        render::EffectsSceneComplete();
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        Picture pixels(kWidth * kHeight * 4);
        for (uint32_t row = 0; row < kHeight; ++row)
            std::memcpy(&pixels[row * kWidth * 4], static_cast<uint8_t *>(locked.pBits) + row * locked.Pitch, kWidth * 4);
        readback_->UnlockRect();
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    Device &d_;
    shim::Surface *readback_ = nullptr, *back_ = nullptr;
    shim::VertexShader *vs_ = nullptr;
    shim::PixelShader *ps_ = nullptr;
    shim::VolumeTexture *bump_ = nullptr;
    shim::CubeTexture *env_ = nullptr;
};

bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

// Settles a settings change, then renders a few frames (the programs and the self-test come first).
Picture settled(Scene &scene, const std::vector<render::EffectsOption> &options)
{
    render::ConfigureEffects(2, options);
    for (int i = 0; i < kMaxSwitchFrames && render::CurrentEffects().pending; ++i)
        scene.render();
    Picture picture;
    for (int i = 0; i < 3; ++i)
        picture = scene.render();
    return picture;
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL video: %s (an X display is needed)\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("sea_edge_check", 0, 0, kWidth, kHeight, 0);
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
            Scene scene(*device);
            // The render scale and MSAA of «Высокая» stay: the capture must match the scene store, edge pixels
            // multisampled. The specular is left out (no sun here).
            const Picture soft = settled(scene, {{"fx_sea_soft_edges", 1}});
            ok &= expect(render::CurrentEffects().summary.find("sea=") != std::string::npos, "«Высокая» active");
            const Picture hard = settled(scene, {{"fx_sea_soft_edges", 0}});
            uint32_t worst = 0, differing = 0, sea = 0;
            for (size_t i = 0; i < soft.size(); i += 4)
            {
                uint32_t d = 0;
                for (int ch = 0; ch < 3; ++ch)
                    d = std::max<uint32_t>(d, std::abs(int(soft[i + ch]) - int(hard[i + ch])));
                worst = std::max(worst, d);
                differing += d > 2;
                // Water: neither the magenta under it nor the grey post.
                const uint8_t *p = &hard[i];
                sea += !(p[0] > 200 && p[2] > 200 && p[1] < 80) && !(std::abs(p[0] - p[1]) < 12 && std::abs(p[1] - p[2]) < 12);
            }
            std::printf("     soft edges on/off: %u pixels differ by more than 2 (worst %u), %u water pixels\n", differing,
                        worst, sea);
            ok &= expect(sea > kWidth * kHeight / 4, "the sea is drawn below the horizon");
            ok &= expect(differing == 0, "soft edges fade no water next to an object in front of the sea");
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: sea_edge_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
