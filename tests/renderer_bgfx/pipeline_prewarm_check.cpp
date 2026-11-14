// The scenes' Vulkan pipelines (src/shim/pipeline_sets.hpp, patches/bgfx-vulkan-pipeline-cache.patch) through the
// shim in an SDL window, on Vulkan (on OpenGL ES the same calls must do nothing). Every frame's pipelines are new:
//   1. the interface never waits: frames whose only new pipelines are the interface's (a pre-transformed quad, and
//      a 3D draw after the frame's interface marked the end of the scene) leave no draw out;
//   2. the scene does: a new 3D scene draw before the interface is left out (one draw, never the interface drawn
//      with it) until its pipeline exists, and its pipeline is recorded under the scene;
//   3. the next start of the device prewarms the scene's recorded pipelines when the scene begins: the fade-in
//      waits for them (HoldSceneReveal), and then the scene's draws leave nothing out;
//   4. a corrupt pipelines.bin is reported and removed, and the scene records again; where the device has pipeline
//      creation cache control, the scene's pipeline, still in the pipeline cache, leaves no draw out.
#include "device.hpp"

#include "check_backend.hpp"

#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"
#include "storm/render/pipelines.hpp"
#include "storm/render/program_cache.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <fstream>
#include <thread>
#include "texture/core/cache.hpp"
#include <unistd.h>
#include <vector>

// patches/bgfx-vulkan-pipeline-cache.patch.
namespace bgfx
{
uint32_t getVkDrawsLeftOut();
uint32_t getVkPipelinesPending();
bool getVkPipelineCacheControl();
} // namespace bgfx

using namespace storm;
using shim::Device;
namespace fs = std::filesystem;

namespace
{

constexpr uint32_t kWidth = 96, kHeight = 64;
constexpr const char *kScene = "check/scene";

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

struct SceneVertex
{
    float x, y, z;
    uint32_t color;
};
struct UiVertex
{
    float x, y, z, rhw;
    uint32_t color;
};

// Blend factor pairs: each gives the draws a pipeline of its own.
constexpr DWORD kBlends[][2] = {{D3DBLEND_ONE, D3DBLEND_ZERO},          {D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA},
                                {D3DBLEND_ONE, D3DBLEND_ONE},           {D3DBLEND_DESTCOLOR, D3DBLEND_ZERO},
                                {D3DBLEND_SRCALPHA, D3DBLEND_ONE},      {D3DBLEND_ZERO, D3DBLEND_SRCCOLOR},
                                {D3DBLEND_INVDESTCOLOR, D3DBLEND_ONE},  {D3DBLEND_ONE, D3DBLEND_INVSRCALPHA},
                                {D3DBLEND_SRCCOLOR, D3DBLEND_DESTCOLOR}, {D3DBLEND_INVSRCCOLOR, D3DBLEND_ZERO}};

void blend(Device &d, int which)
{
    d.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d.SetRenderState(D3DRS_SRCBLEND, kBlends[which][0]);
    d.SetRenderState(D3DRS_DESTBLEND, kBlends[which][1]);
}

// One frame: an optional 3D scene draw with blend `scene` (< 0: none), then the interface: a pre-transformed quad
// with blend `ui_quad` and a 3D draw after the interface mark with blend `ui_3d` (< 0: none).
void frame(Device &d, int scene, int ui_quad, int ui_3d)
{
    check(d.BeginScene(), "BeginScene");
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
    d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d.SetRenderState(D3DRS_LIGHTING, FALSE);
    d.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d.SetTexture(0, nullptr);
    const SceneVertex tri[3] = {{-4, -4, 0, 0xFFC08040}, {4, -4, 0, 0xFF40C080}, {0, 4, 0, 0xFF8040C0}};
    d.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    if (scene >= 0)
    {
        d.SetRenderState(D3DRS_ZENABLE, TRUE);
        blend(d, scene);
        check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(SceneVertex)), "draw (scene)");
    }
    render::EffectsSceneComplete();
    d.SetRenderState(D3DRS_ZENABLE, FALSE);
    if (ui_3d >= 0)
    {
        blend(d, ui_3d);
        check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(SceneVertex)), "draw (3D in the interface)");
    }
    const UiVertex ui[3] = {{0, 0, 0, 1, 0xFFE0E0E0}, {24, 0, 0, 1, 0xFFE0E0E0}, {0, 16, 0, 1, 0xFFE0E0E0}};
    d.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    blend(d, ui_quad);
    check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, ui, sizeof(UiVertex)), "draw (interface quad)");
    check(d.EndScene(), "EndScene");
    check(d.Present(), "Present");
}

struct Renderer
{
    Device *device = nullptr;
    explicit Renderer(SDL_Window *window)
    {
        if (!shim::Context::instance().init(window, kWidth, kHeight, false))
        {
            std::fprintf(stderr, "FAIL: no renderer\n");
            std::exit(1);
        }
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        device = new Device(window, params);
    }
    ~Renderer()
    {
        device->Release(); // shuts the context down with the device
        shim::Context::instance().shutdown();
    }
};

using Clock = std::chrono::steady_clock;
constexpr std::chrono::seconds kPatience{30}; // pipeline threads run at low priority: a loaded host makes them slow

struct Settled
{
    uint32_t left_out; // draws left out on the way
    uint32_t after;    // draws left out in the last frames, once nothing was pending
};

// Frames of `scene` (blend) until nothing is pending (by time, not by frame count: frames run far faster than a
// starved pipeline thread).
Settled settle(Device &d, int scene)
{
    const uint32_t before = bgfx::getVkDrawsLeftOut();
    for (int i = 0; i < 4; ++i)
        frame(d, scene, 0, 0);
    const auto end = Clock::now() + kPatience;
    while (bgfx::getVkPipelinesPending() != 0 && Clock::now() < end)
        frame(d, scene, 0, 0);
    const uint32_t settled = bgfx::getVkDrawsLeftOut();
    for (int i = 0; i < 4; ++i)
        frame(d, scene, 0, 0);
    return {bgfx::getVkDrawsLeftOut() - before, bgfx::getVkDrawsLeftOut() - settled};
}


// A pipelines.bin of the scene "start" with one recorded pipeline of shaders no shim program is made of (the way
// the interface's programs are: made by their own code, later than the first frame).
void write_waiting_recipe(const fs::path &file)
{
    std::vector<uint8_t> bytes;
    const auto put = [&bytes](const void *data, size_t size) {
        bytes.insert(bytes.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    };
    const auto put_string = [&](const std::string &text) {
        const uint16_t n = static_cast<uint16_t>(text.size());
        put(&n, sizeof(n));
        put(text.data(), text.size());
    };
    const char magic[8] = {'S', 'T', 'P', 'I', 'P', 'E', 'S', 'T'};
    put(magic, sizeof(magic));
    const uint32_t version = 1, one = 1, hash = 0x5EC12E7u, recipe_size = 4;
    const uint8_t recipe[4] = {1, 2, 3, 4}; // not a recipe bgfx takes: the prewarm, once asked for, is refused
    put(&version, 4);
    put(&one, 4);
    put(&hash, 4);
    put_string("ext_vs");
    put_string("ext_fs");
    put(&recipe_size, 4);
    put(recipe, 4);
    put(&one, 4);
    put_string("start");
    put(&one, 4);
    put(&hash, 4);
    const uint64_t sum = storm::texture::hash64(bytes.data(), bytes.size(), 0x5354504950ull);
    put(&sum, 8);
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()),
                                                  static_cast<std::streamsize>(bytes.size()));
}

bool file_has_waiting_recipe(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    return text.find("ext_vs") != std::string::npos;
}

// Polls (the file is written by a thread of its own) until the recipe is gone from the file.
bool waiting_recipe_goes(const fs::path &file)
{
    for (const auto end = Clock::now() + std::chrono::seconds(5); Clock::now() < end;
         std::this_thread::sleep_for(std::chrono::milliseconds(20)))
        if (!file_has_waiting_recipe(file))
            return true;
    return false;
}

bool vulkan(SDL_Window *window, const fs::path &root)
{
    bool ok = true;
    {
        Renderer r(window);
        Device &d = *r.device;
        render::BeginScenePipelines(kScene);
        settle(d, -1); // the clear and the first interface pipelines
        // 1. Only the interface has new pipelines: nothing may be left out.
        uint32_t before = bgfx::getVkDrawsLeftOut();
        for (int i = 1; i < 5; ++i)
            frame(d, -1, i, i + 5);
        frame(d, -1, 0, 0); // its bgfx::frame() waits until the render thread finished the frames before
        ok &= expect(bgfx::getVkDrawsLeftOut() == before, "new interface pipelines: no interface draw is left out");
        // 2. A new scene pipeline with new interface pipelines in the same frame: the scene draw alone is left out.
        before = bgfx::getVkDrawsLeftOut();
        frame(d, 3, 5, 6);
        frame(d, -1, 0, 0);
        const uint32_t first = bgfx::getVkDrawsLeftOut() - before;
        ok &= expect(first == 1, "a new scene pipeline: its draw is left out, the interface drawn with it is not");
        std::printf("     left out in the first frame: %u\n", first);
        const Settled later = settle(d, 3);
        ok &= expect(later.after == 0, "the scene draw appears once its pipeline exists");
    }
    const fs::path sets = [&] {
        for (const auto &item : fs::recursive_directory_iterator(root))
            if (item.path().filename() == "pipelines.bin")
                return item.path();
        return fs::path{};
    }();
    ok &= expect(!sets.empty(), "the scene's pipelines are kept (pipelines.bin next to the pipeline cache)");
    {
        // 3. A new start: the recorded scene pipeline is prewarmed while the scene loads.
        Renderer r(window);
        Device &d = *r.device;
        render::BeginScenePipelines(kScene);
        int held = 0;
        const auto end = Clock::now() + kPatience;
        do
        {
            frame(d, -1, 0, 0); // the loading picture
            ++held;
        } while (render::HoldSceneReveal() && Clock::now() < end);
        std::printf("     the fade-in waited %d frames\n", held);
        ok &= expect(!render::HoldSceneReveal(), "the fade-in ends once the scene's pipelines exist");
        const uint32_t before = bgfx::getVkDrawsLeftOut();
        for (int i = 0; i < 4; ++i)
            frame(d, 3, 0, 0);
        frame(d, -1, 0, 0);
        ok &= expect(bgfx::getVkDrawsLeftOut() == before, "prewarmed: the scene's draw is not left out");
    }
    {
        // 4. A damaged file: reported, removed; the scene records again.
        std::fstream file(sets, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(20);
        file.put('\x5A');
    }
    {
        Renderer r(window);
        ok &= expect(!fs::exists(sets), "a corrupt pipelines.bin is removed");
        render::BeginScenePipelines(kScene);
        // Nothing recorded now, but the pipeline cache still holds the scene's pipeline: with pipeline creation
        // cache control it is taken from there at the draw, which is not left out.
        const uint32_t left_out = settle(*r.device, 3).left_out;
        // lavapipe keeps no compiled pipelines in its pipeline cache: it always answers that a compile is required.
        const bool caching = shim::Context::instance().device_info().renderer.find("llvmpipe") == std::string::npos;
        if (bgfx::getVkPipelineCacheControl() && caching)
            ok &= expect(left_out == 0, "cache control: a pipeline in the pipeline cache leaves no draw out");
        else
            std::printf("     no pipeline creation cache control, or a pipeline cache without pipelines (lavapipe): %u "
                        "draws left out\n",
                        left_out);
    }
    {
        // 5. A recorded pipeline whose program is not made yet (the interface's programs exist from its init on) is
        // kept, not forgotten, and prewarmed once the program is registered; one whose program never comes goes at
        // the end of the run.
        write_waiting_recipe(sets);
        Renderer r(window);
        Device &d = *r.device;
        for (int i = 0; i < 5; ++i)
            frame(d, -1, 0, 0);
        render::BeginScenePipelines("elsewhere"); // writes the sets if anything was forgotten
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ok &= expect(file_has_waiting_recipe(sets), "a recorded pipeline whose program does not exist yet is kept");
        const bgfx::ProgramHandle program =
            bgfx::createProgram(shim::create_embedded_shader(shim::shader_id_by_name("vs_blit")),
                                shim::create_embedded_shader(shim::shader_id_by_name("fs_blit")), true);
        render::FrameOwner &owner = shim::Context::instance();
        owner.RegisterPipelineProgram("ext_vs", "ext_fs", program.idx);
        for (int i = 0; i < 5; ++i)
            frame(d, -1, 0, 0);
        render::BeginScenePipelines("elsewhere again");
        ok &= expect(waiting_recipe_goes(sets), "registered: it is asked for (refused here: not a bgfx recipe)");
        owner.UnregisterPipelineProgram(program.idx);
        frame(d, -1, 0, 0);
        bgfx::destroy(program);
    }
    {
        write_waiting_recipe(sets);
        {
            Renderer r(window);
            for (int i = 0; i < 5; ++i)
                frame(*r.device, -1, 0, 0);
        }
        ok &= expect(!file_has_waiting_recipe(sets), "a program that never comes: its recipe is forgotten at the end");
    }
    ok &= expect(fs::exists(sets), "and written again");
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
    host_check::UseBackendFromEnvironment();
    // NVIDIA's driver keeps compiled pipelines of its own across runs: every run must start without them.
    setenv("__GL_SHADER_DISK_CACHE", "0", 1);
    SDL_Window *window = SDL_CreateWindow("pipeline_prewarm_check", 0, 0, kWidth, kHeight, 0);
    if (!window)
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    const fs::path root = fs::temp_directory_path() / ("storm-pipeline-prewarm-check-" + std::to_string(getpid()));
    fs::remove_all(root);
    render::ConfigureProgramCache({root, 32ull << 20});
    bool ok = true;
    if (host_check::VulkanAsked())
        ok = vulkan(window, root);
    else
    {
        // OpenGL ES: no pipelines; nothing is held and nothing left out.
        Renderer r(window);
        render::BeginScenePipelines(kScene);
        frame(*r.device, 3, 1, 2);
        ok &= expect(!render::HoldSceneReveal(), "OpenGL ES: no fade-in is held");
    }
    fs::remove_all(root);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: pipeline_prewarm_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
