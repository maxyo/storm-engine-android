// Program binary cache (src/shim/program_cache.hpp, patches/bgfx-program-binary-cache.patch) through OpenGL ES in
// an SDL window (an X display: xvfb-run in CI), and the same cache holding Vulkan's pipeline caches
// (STORM_CHECK_BACKEND=vulkan: one entry per pipeline state, several per program; the device's start loads one
// pipeline of its own, the backend self-test's). Each "run" starts the renderer, switches to «Высокая» (about twenty
// programs: fixed-function variants, post chain, self-test), renders a lit scene with an interface, reads the
// picture back and shuts the renderer down, in one process:
//   1. empty cache: every program linked and stored, none loaded; a stale device directory is removed;
//   2. warm cache: every program loaded, none linked, the same picture;
//   3. one entry corrupt, one truncated: both logged and removed, those two linked and stored again;
//   4. one entry whose binary the driver rejects (valid file, bogus binary format): linked, replaced;
//   5. a small size limit: the oldest entries go, the total stays under it;
//   6. the whole cache deleted while the renderer runs (Android: «Очистить кэш», low storage): nothing counted as
//      invalid, every program linked and stored into the directory made again; the next run loads them.
#include "device.hpp"
#include <functional>
#include "check_backend.hpp"

#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"
#include "storm/render/program_cache.hpp"
#include "texture/core/cache.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

using namespace storm;
using shim::Device;
namespace fs = std::filesystem;

namespace
{

constexpr uint32_t kWidth = 96, kHeight = 64;

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
    float x, y, z, nx, ny, nz;
    uint32_t color;
};
struct UiVertex
{
    float x, y, z, rhw;
    uint32_t color;
};
using Picture = std::vector<uint8_t>;

Picture frame(Device &d, storm::shim::Surface *back, storm::shim::Surface *readback)
{
    check(d.BeginScene(), "BeginScene");
    check(d.SetRenderTarget(0, back), "SetRenderTarget");
    check(d.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF2050A0, 1.0f, 0), "Clear");
    D3DMATRIX identity{};
    identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
    D3DMATRIX world = identity;
    world.m[3][2] = 5.0f;
    D3DMATRIX projection{};
    projection.m[0][0] = static_cast<float>(kHeight) / kWidth;
    projection.m[1][1] = 1.0f;
    projection.m[2][2] = 100.0f / 99.0f;
    projection.m[2][3] = 1.0f;
    projection.m[3][2] = -100.0f / 99.0f;
    d.SetTransform(D3DTS_WORLD, &world);
    d.SetTransform(D3DTS_VIEW, &identity);
    d.SetTransform(D3DTS_PROJECTION, &projection);
    d.SetRenderState(D3DRS_ZENABLE, TRUE);
    d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d.SetRenderState(D3DRS_LIGHTING, TRUE);
    d.SetRenderState(D3DRS_AMBIENT, 0xFF303030);
    d.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d.SetTexture(0, nullptr);
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = {0.9f, 0.85f, 0.8f, 1};
    light.Direction = {0.2f, -0.3f, 1.0f};
    d.SetLight(0, &light);
    d.LightEnable(0, TRUE);
    const SceneVertex quad[6] = {
        {-6, -6, 1, 0.3f, 0.2f, -1, 0xFFC08040}, {6, -6, -1, -0.3f, 0.2f, -1, 0xFF40C080},
        {-6, 6, 1, 0.3f, -0.2f, -1, 0xFF8040C0}, {-6, 6, 1, 0.3f, -0.2f, -1, 0xFF8040C0},
        {6, -6, -1, -0.3f, 0.2f, -1, 0xFF40C080}, {6, 6, -1, -0.3f, -0.2f, -1, 0xFFC0C040},
    };
    d.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
    check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(SceneVertex)), "draw (scene)");
    render::EffectsSceneComplete();
    d.SetRenderState(D3DRS_ZENABLE, FALSE);
    d.SetRenderState(D3DRS_LIGHTING, FALSE);
    const UiVertex ui[6] = {{0, 0, 0, 1, 0xFFE0E0E0}, {24, 0, 0, 1, 0xFFE0E0E0}, {0, 16, 0, 1, 0xFFE0E0E0},
                            {0, 16, 0, 1, 0xFFE0E0E0}, {24, 0, 0, 1, 0xFFE0E0E0}, {24, 16, 0, 1, 0xFFE0E0E0}};
    d.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, ui, sizeof(UiVertex)), "draw (interface)");
    check(d.EndScene(), "EndScene");
    check(d.GetRenderTargetData(back, readback), "GetRenderTargetData");
    D3DLOCKED_RECT locked;
    check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect");
    Picture pixels(kWidth * kHeight * 4);
    for (uint32_t y = 0; y < kHeight; ++y)
        std::memcpy(&pixels[y * kWidth * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kWidth * 4);
    readback->UnlockRect();
    check(d.Present(), "Present");
    return pixels;
}

struct Run
{
    Picture picture;
    render::ProgramCacheStats stats; // of this run only
    int switch_frames = 0;           // frames until «Высокая» was active
};

// One renderer lifetime: «Высокая» prepared and drawn; `running` is called once the device has read the cache.
Run run(SDL_Window *window, const std::function<void()> &running = {})
{
    const render::ProgramCacheStats before = render::CurrentProgramCacheStats();
    if (!shim::Context::instance().init(window, kWidth, kHeight, false))
    {
        std::fprintf(stderr, "FAIL: no OpenGL ES context\n");
        std::exit(1);
    }
    Run result;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        auto *device = new Device(window, params);
        storm::shim::Surface *readback = nullptr, *back = nullptr;
        check(device->CreateOffscreenPlainSurface(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr),
              "CreateOffscreenPlainSurface");
        check(device->GetRenderTarget(0, &back), "GetRenderTarget");
        if (running)
            running();
        render::ConfigureEffects(2, {});
        for (; result.switch_frames < 120 && render::CurrentEffects().pending; ++result.switch_frames)
            frame(*device, back, readback);
        for (int i = 0; i < 2; ++i)
            result.picture = frame(*device, back, readback);
        readback->Release();
        back->Release();
        device->Release(); // shuts the context down with the device
    }
    shim::Context::instance().shutdown();
    const render::ProgramCacheStats after = render::CurrentProgramCacheStats();
    result.stats = after;
    result.stats.loaded = after.loaded - before.loaded;
    result.stats.stored = after.stored - before.stored;
    result.stats.replaced = after.replaced - before.replaced;
    result.stats.invalid = after.invalid - before.invalid;
    std::printf("     run: «Высокая» after %d frames; %u loaded, %u stored, %u replaced, %u invalid; %u binaries, %.1f KB\n",
                result.switch_frames, result.stats.loaded, result.stats.stored, result.stats.replaced,
                result.stats.invalid, result.stats.entries, result.stats.bytes / 1024.0);
    return result;
}

uint32_t max_difference(const Picture &a, const Picture &b)
{
    uint32_t worst = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            worst = std::max<uint32_t>(worst, std::abs(int(a[i + c]) - int(b[i + c])));
    return worst;
}

bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

// The cache files, most recently used first (a load touches its file): the first ones are programs every run uses.
std::vector<fs::path> entries(const fs::path &root)
{
    std::vector<fs::path> files;
    for (const auto &item : fs::recursive_directory_iterator(root))
        if (item.is_regular_file())
            files.push_back(item.path());
    std::sort(files.begin(), files.end(),
              [](const fs::path &a, const fs::path &b) { return fs::last_write_time(a) > fs::last_write_time(b); });
    return files;
}

std::vector<char> read(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void write(const fs::path &file, const std::vector<char> &bytes)
{
    std::ofstream(file, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
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
    SDL_Window *window = SDL_CreateWindow("program_cache_check", 0, 0, kWidth, kHeight, 0);
    if (!window)
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    const fs::path root = fs::temp_directory_path() / ("storm-program-cache-check-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "0123456789abcdef");
    write(root / "0123456789abcdef" / "00000000000000aa.bin", {'o', 'l', 'd'});
    render::ConfigureProgramCache({root, 32ull << 20});
    bool ok = true;

    const Run cold = run(window);
    if (cold.stats.stored == 0)
    {
        std::printf("FAIL: the driver stored no program binary (GL_NUM_PROGRAM_BINARY_FORMATS 0?)\n");
        return 1;
    }
    ok &= expect(cold.stats.loaded == 0, "empty cache: nothing loaded, every program linked and stored");
    ok &= expect(!fs::exists(root / "0123456789abcdef"), "the directory of another device is removed");

    const bool vulkan = host_check::VulkanAsked();
    const Run warm = run(window);
    // The first run also ran the GPU self-test (OpenGL ES: its copy program; Vulkan: the pipelines of its targets);
    // the later ones do not (its result is kept).
    ok &= expect(warm.stats.stored == 0 && warm.stats.loaded > 0 && (vulkan || warm.stats.loaded + 1 >= cold.stats.stored),
                 "warm cache: every program loaded, none linked");
    ok &= expect(max_difference(warm.picture, cold.picture) <= 2, "warm cache: the same picture");
    // How long a driver takes to build a pipeline without its cache is the driver's own: not compared on Vulkan.
    if (!vulkan)
        ok &= expect(warm.switch_frames * 2 < cold.switch_frames,
                     "warm cache: «Высокая» prepared in fewer frames (cached binaries need no link)");

    auto files = entries(root);
    {
        auto corrupt = read(files[0]);
        corrupt.back() ^= 0x5A;
        write(files[0], corrupt);
        auto truncated = read(files[1]);
        truncated.resize(truncated.size() / 2);
        write(files[1], truncated);
    }
    const Run damaged = run(window);
    ok &= expect(damaged.stats.invalid == 2 && damaged.stats.stored == 2 &&
                     damaged.stats.loaded == warm.stats.loaded - 2,
                 "a corrupt and a truncated entry: removed and linked again, the rest loaded");
    ok &= expect(max_difference(damaged.picture, cold.picture) <= 2, "damaged cache: the same picture");

    {
        // A well-formed entry whose binary the driver cannot take: bgfx's GLenum format word made bogus, the
        // file checksum recomputed (header layout of src/shim/program_cache.cpp: 40 bytes, checksum last).
        files = entries(root);
        auto bytes = read(files[2]);
        uint32_t bogus = 0xDEADu;
        std::memcpy(bytes.data() + 40, &bogus, sizeof(bogus));
        const uint64_t sum = texture::hash64(bytes.data() + 40, bytes.size() - 40, 0x5354505247u);
        std::memcpy(bytes.data() + 32, &sum, sizeof(sum));
        write(files[2], bytes);
    }
    const Run rejected = run(window);
    ok &= expect(rejected.stats.replaced == 1 && rejected.stats.stored == 1 && rejected.stats.invalid == 0,
                 "a binary the driver rejects: linked from the shaders and replaced");
    ok &= expect(max_difference(rejected.picture, cold.picture) <= 2, "rejected binary: the same picture");

    const uint64_t limit = warm.stats.bytes / 3;
    render::ConfigureProgramCache({root, limit});
    const Run small = run(window);
    ok &= expect(small.stats.bytes <= limit && small.stats.entries < warm.stats.entries,
                 "a small limit: the oldest entries go, the total stays under it");

    render::ConfigureProgramCache({root, 32ull << 20});
    const Run filled = run(window);
    const Run cleared = run(window, [&] { fs::remove_all(root); });
    // Vulkan: the device's start loaded the self-test's pipeline before the cache went.
    ok &= expect(cleared.stats.invalid == 0 && cleared.stats.loaded <= (vulkan ? 1u : 0u) &&
                     cleared.stats.stored + 1 >= filled.stats.entries,
                 "cache deleted while running: nothing invalid, every program linked and stored again");
    ok &= expect(!entries(root).empty(), "cache deleted while running: the directory is made again");
    ok &= expect(max_difference(cleared.picture, cold.picture) <= 2, "cache deleted while running: the same picture");
    const Run refilled = run(window);
    // Vulkan: the pipeline the cleared run had loaded before the cache went was not written again by that run.
    ok &= expect(refilled.stats.stored <= (vulkan ? 1u : 0u) && refilled.stats.loaded + 1 >= filled.stats.entries,
                 "after the deletion: the next run loads every program");

    fs::remove_all(root);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: program_cache_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
