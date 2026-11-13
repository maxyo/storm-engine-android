// Game buffers in bgfx's shared dynamic buffers (Device::upload_buffer, src/shim/draw.cpp) through the shim on OpenGL
// ES in an SDL window (an X display, like skinning_check).
//
// A scene or a screen opening writes many small vertex and index buffers once and draws them from the next frame on.
// Each once got its own GL buffer; now they share bgfx's few. The check draws such buffers of three vertex strides and
// both index sizes, buffers created after others were released, buffers partly rewritten, and a buffer drawn with
// another stride than at first, and compares every picture with the same geometry drawn from client memory
// (DrawIndexedPrimitiveUP): each range must address exactly its buffer's data. Only the shared buffers may be created.
#include "device.hpp"
#include "check_backend.hpp"

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

constexpr uint32_t kWidth = 160, kHeight = 96;
constexpr uint32_t kColumns = 16, kRows = 12; // 10x8 pixel cells
constexpr uint32_t kBuffers = kColumns * kRows;

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

using Picture = std::vector<uint8_t>;

// One quad in a grid cell; the vertex format varies with the cell, so the buffers have different strides.
struct Quad
{
    DWORD fvf;
    UINT stride;
    std::vector<uint8_t> vertices; // 4 vertices
    bool index32;
    std::vector<uint8_t> indices; // 6 indices
    shim::VertexBuffer *vb = nullptr;
    shim::IndexBuffer *ib = nullptr;
};

Quad make_quad(uint32_t cell, uint32_t seed)
{
    static const DWORD fvfs[3] = {D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                                  D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR};
    static const UINT strides[3] = {20, 28, 24};
    const uint32_t kind = (cell + seed) % 3;
    Quad quad{fvfs[kind], strides[kind], std::vector<uint8_t>(strides[kind] * 4), (cell + seed) % 2 == 1, {}};
    const float x0 = static_cast<float>(cell % kColumns * 10 + 1), y0 = static_cast<float>(cell / kColumns * 8 + 1);
    const uint32_t color = 0xFF000000u | ((cell * 37 + seed * 91) % 256) << 16 | ((cell * 11 + seed * 53) % 256) << 8 |
                           (cell * 73 + seed * 17) % 256;
    const float corner[4][2] = {{0, 0}, {8, 0}, {0, 6}, {8, 6}};
    for (uint32_t v = 0; v < 4; ++v)
    {
        uint8_t *out = quad.vertices.data() + v * quad.stride;
        const float position[4] = {x0 + corner[v][0], y0 + corner[v][1], 0.5f, 1.0f};
        std::memcpy(out, position, 16);
        std::memcpy(out + 16, &color, 4);
        if (kind == 2)
            std::memset(out + 20, 0, 4); // specular
    }
    const uint32_t order[6] = {0, 1, 2, 2, 1, 3};
    quad.indices.resize(quad.index32 ? 24 : 12);
    for (uint32_t i = 0; i < 6; ++i)
        if (quad.index32)
            std::memcpy(&quad.indices[i * 4], &order[i], 4);
        else
        {
            const uint16_t index = static_cast<uint16_t>(order[i]);
            std::memcpy(&quad.indices[i * 2], &index, 2);
        }
    return quad;
}

class Scene
{
  public:
    explicit Scene(Device &device) : d_(device)
    {
        check(d_.CreateOffscreenPlainSurface(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Scene()
    {
        readback_->Release();
        back_->Release();
    }

    void create(Quad &quad)
    {
        void *data = nullptr;
        check(d_.CreateVertexBuffer(static_cast<UINT>(quad.vertices.size()), D3DUSAGE_WRITEONLY, quad.fvf,
                                    D3DPOOL_MANAGED, &quad.vb, nullptr),
              "CreateVertexBuffer");
        check(quad.vb->Lock(0, 0, &data, 0), "Lock(vb)");
        std::memcpy(data, quad.vertices.data(), quad.vertices.size());
        quad.vb->Unlock();
        check(d_.CreateIndexBuffer(static_cast<UINT>(quad.indices.size()), D3DUSAGE_WRITEONLY,
                                   quad.index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, D3DPOOL_MANAGED, &quad.ib, nullptr),
              "CreateIndexBuffer");
        check(quad.ib->Lock(0, 0, &data, 0), "Lock(ib)");
        std::memcpy(data, quad.indices.data(), quad.indices.size());
        quad.ib->Unlock();
    }

    static void release(Quad &quad)
    {
        quad.vb->Release();
        quad.ib->Release();
        quad.vb = nullptr;
        quad.ib = nullptr;
    }

    // Every quad that has buffers: from them, or from client memory.
    Picture render(const std::vector<Quad> &quads, bool from_buffers)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        d_.SetTexture(0, nullptr);
        for (const Quad &quad : quads)
        {
            if (!quad.vb)
                continue;
            d_.SetFVF(quad.fvf);
            if (from_buffers)
            {
                d_.SetStreamSource(0, quad.vb, 0, quad.stride);
                d_.SetIndices(quad.ib);
                check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2), "DrawIndexedPrimitive");
            }
            else
                check(d_.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, quad.indices.data(),
                                                quad.index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, quad.vertices.data(),
                                                quad.stride),
                      "DrawIndexedPrimitiveUP");
        }
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
};

uint32_t differing(const Picture &a, const Picture &b)
{
    uint32_t count = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        count += std::memcmp(&a[i], &b[i], 3) != 0;
    return count;
}

// GL buffers bgfx holds (the shared dynamic buffers among them, not the ranges in them), after the last frame.
uint32_t gl_buffers()
{
    const bgfx::Stats *stats = bgfx::getStats();
    return stats->numVertexBuffers + stats->numIndexBuffers;
}

uint32_t lit(const Picture &picture)
{
    uint32_t count = 0;
    for (size_t i = 0; i < picture.size(); i += 4)
        count += (picture[i] | picture[i + 1] | picture[i + 2]) != 0;
    return count;
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
    SDL_Window *window = SDL_CreateWindow("buffer_pool_check", 0, 0, kWidth, kHeight, 0);
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
            std::vector<Quad> quads;
            for (uint32_t cell = 0; cell < kBuffers; ++cell)
                quads.push_back(make_quad(cell, 0));
            // The programs of both paths first.
            scene.render(quads, false);
            scene.render(quads, false);

            // An interface opening: every buffer written in one frame, drawn from the next one on.
            for (Quad &quad : quads)
                scene.create(quad);
            scene.render({}, false);
            const Picture reference = scene.render(quads, false);
            uint32_t before = gl_buffers();
            const Picture pooled = scene.render(quads, true);
            const uint32_t creates = gl_buffers() - before;
            std::printf("     %u buffers of each kind: %u GL buffers created, %u of %u pixels differ\n", kBuffers, creates,
                        differing(pooled, reference), kWidth * kHeight);
            ok &= expect(lit(reference) > kWidth * kHeight / 2, "the quads are drawn");
            ok &= expect(differing(pooled, reference) == 0, "pooled buffers draw what their data says");
            // One shared vertex buffer and one of each index size.
            ok &= expect(creates <= 2, "they share at most one new GL buffer of each kind");
            const render::PerfCounters start = render::PerfTotals();
            const Picture again = scene.render(quads, true);
            ok &= expect(differing(again, reference) == 0 && render::PerfTotals().Since(start).buffer_uploads == 0,
                         "a later frame draws them again without uploads");

            // A screen closing: every other buffer released; new ones written a frame later, in the freed ranges.
            for (uint32_t cell = 0; cell < kBuffers; cell += 2)
                Scene::release(quads[cell]);
            scene.render(quads, true);
            for (uint32_t cell = 0; cell < kBuffers; cell += 2)
            {
                quads[cell] = make_quad(cell, 1);
                scene.create(quads[cell]);
            }
            scene.render({}, false);
            const Picture reused_reference = scene.render(quads, false);
            before = gl_buffers();
            const Picture reused = scene.render(quads, true);
            const uint32_t reused_creates = gl_buffers() - before;
            std::printf("     buffers in released ranges: %u GL buffers created, %u pixels differ\n", reused_creates,
                        differing(reused, reused_reference));
            ok &= expect(differing(reused, reused_reference) == 0, "buffers in released ranges draw their own data");
            ok &= expect(reused_creates == 0, "they share the same GL buffers");

            // A rewrite of one vertex of every third quad (its colour), drawn from the next frame on: updated in place.
            for (uint32_t cell = 1; cell < kBuffers; cell += 3)
            {
                Quad &quad = quads[cell];
                const uint32_t color = 0xFF20E0A0u;
                for (uint32_t v = 2; v < 4; ++v)
                    std::memcpy(quad.vertices.data() + v * quad.stride + 16, &color, 4);
                void *data = nullptr;
                check(quad.vb->Lock(2 * quad.stride, 2 * quad.stride, &data, 0), "Lock(rewrite)");
                std::memcpy(data, quad.vertices.data() + 2 * quad.stride, 2 * quad.stride);
                quad.vb->Unlock();
            }
            scene.render({}, false);
            const Picture rewritten_reference = scene.render(quads, false);
            before = gl_buffers();
            const render::PerfCounters rewrite_start = render::PerfTotals();
            const Picture rewritten = scene.render(quads, true);
            const render::PerfCounters rewrite = render::PerfTotals().Since(rewrite_start);
            std::printf("     rewritten: %llu uploads, %llu bgfx buffers created, %u pixels differ\n",
                        static_cast<unsigned long long>(rewrite.buffer_uploads),
                        static_cast<unsigned long long>(rewrite.buffer_creates), differing(rewritten, rewritten_reference));
            ok &= expect(differing(rewritten, rewritten_reference) == 0, "partly rewritten buffers draw their new data");
            ok &= expect(rewrite.buffer_creates == 0 && gl_buffers() == before, "a rewrite updates in place");

            // A buffer drawn with another stride than at first: drawn as XYZRHW+DIFFUSE (20 bytes), then as the 28-byte
            // vertices its bytes hold.
            std::vector<Quad> moved(2);
            moved[0] = make_quad(0, 0);
            moved[1] = make_quad(1, 0); // (1 + 0) % 3 == 1: 28-byte vertices
            Quad as20 = moved[1];
            as20.fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
            as20.stride = 20;
            for (Quad &quad : moved)
                scene.create(quad);
            scene.render({}, false);
            as20.vb = moved[1].vb;
            as20.ib = moved[1].ib;
            scene.render({moved[0], as20}, true); // a scrambled quad: the 28-byte vertices read in steps of 20
            const Picture moved_reference = scene.render(moved, false);
            before = gl_buffers();
            const Picture moved_picture = scene.render(moved, true);
            const uint32_t moved_creates = gl_buffers() - before;
            std::printf("     drawn with another stride: %u GL buffers created\n", moved_creates);
            ok &= expect(differing(moved_picture, moved_reference) == 0 && lit(moved_reference) > 0,
                         "a buffer drawn with another stride draws its data");
            ok &= expect(moved_creates == 0, "it is created again in the shared buffers");
            for (Quad &quad : moved)
                Scene::release(quad);
            for (Quad &quad : quads)
                Scene::release(quad);
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: buffer_pool_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
