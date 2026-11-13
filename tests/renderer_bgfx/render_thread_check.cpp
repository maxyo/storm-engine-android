// bgfx's render thread through the shim (src/shim/device.hpp, Context; patches/bgfx-render-thread-hooks.patch) on
// OpenGL ES in an SDL window (an X display: xvfb-run in CI), with the render thread and with render_thread=0:
//   1. the device reports the mode asked for, and the driver's strings read on the render thread;
//   2. frames present with the game thread taking turns with the render thread, and overlapping it;
//   3. a surface release from another thread (Android's surfaceDestroyed) returns only when no frame is in flight,
//      presentation stops, and resumes after the surface is restored: many times, at any point of a frame;
//   4. the renderer shuts down and starts again.
#include "device.hpp"
#include "storm/render/frame_owner.hpp"

#include <SDL2/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace storm;
using shim::Context;

namespace
{
constexpr uint32_t kWidth = 96, kHeight = 64;

bool expect(bool condition, const char *mode, const char *what)
{
    std::printf("%s: %s: %s\n", condition ? "ok" : "FAIL", mode, what);
    return condition;
}

void present(Context &context, int frames)
{
    for (int i = 0; i < frames; ++i)
        context.present(BGFX_INVALID_HANDLE, 0, 0);
}

bool run(SDL_Window *window, bool render_thread)
{
    const char *mode = render_thread ? "render thread" : "render_thread=0";
    Context &context = Context::instance();
    bool ok = true;
    for (int start = 0; start < 2; ++start)
    {
        render::SetRenderThread(render_thread);
        render::SetFrameOverlap(-1);
        if (!context.init(window, kWidth, kHeight, false))
            return expect(false, mode, "the device starts");
        ok &= expect(context.render_thread() == render_thread, mode, "bgfx runs in the mode asked for");
        ok &= expect(!context.gl_info().renderer.empty() && context.gl_info().renderer != "?" &&
                         context.gl_info().version.find("OpenGL ES") != std::string::npos &&
                         !context.gl_info().extensions.empty(),
                     mode, "the driver's strings are known after init");
        present(context, 20);
        render::SetFrameOverlap(0);
        present(context, 20);
        render::SetFrameOverlap(1);
        present(context, 20);
        render::SetFrameOverlap(-1);
        ok &= expect(context.ready(), mode, "frames present taking turns and overlapped");

        // Android's UI thread asks for the surface at any point of the game thread's frames.
        std::atomic<bool> stop{false}, flying{false}, slow{false};
        std::atomic<int> releases{0};
        std::thread ui([&] {
            for (int i = 0; i < 40 && !stop; ++i)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(300 + 173 * i));
                const auto before = std::chrono::steady_clock::now();
                context.request_surface_release();
                if (context.frames_in_flight())
                    flying = true;
                if (std::chrono::steady_clock::now() - before > std::chrono::seconds(2))
                    slow = true;
                ++releases;
                context.request_surface_restore();
                // Until the game thread has restored the surface.
                for (int wait = 0; wait < 2000 && !context.ready() && !stop; ++wait)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            stop = true;
        });
        int presented = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!stop && std::chrono::steady_clock::now() < deadline)
        {
            context.present(BGFX_INVALID_HANDLE, 0, 0); // polls the surface: honours the release, restores
            ++presented;
        }
        stop = true;
        ui.join();
        ok &= expect(releases == 40, mode, "40 surface releases and restores while frames are presented");
        ok &= expect(!flying, mode, "no frame is in flight when a surface release returns");
        ok &= expect(!slow, mode, "no surface release waited out its time limit");
        context.surface_restored();
        present(context, 5);
        ok &= expect(context.ready() && presented > 40, mode, "presentation resumes after the last restore");
        context.shutdown();
        ok &= expect(!context.initialised() && !context.frames_in_flight(), mode, "the renderer shuts down");
    }
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
    SDL_Window *window = SDL_CreateWindow("render_thread_check", 0, 0, kWidth, kHeight, 0);
    if (!window)
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = run(window, true);
    ok &= run(window, false);
    ok &= run(window, true); // and back: bgfx forgets the single-threaded choice at shutdown
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: render thread\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
