// bgfx's render thread through the shim (src/shim/device.hpp, Context; patches/bgfx-render-thread-hooks.patch) on
// OpenGL ES in an SDL window (an X display: xvfb-run in CI), with the render thread and with render_thread=0:
//   1. the device reports the mode asked for and the device's strings (OpenGL ES: read on the render thread), and
//      the backend self-test passes;
//   2. frames present with the game thread taking turns with the render thread, and overlapping it;
//   3. a surface release from another thread (Android's surfaceDestroyed) returns only when no frame is in flight,
//      presentation stops, and resumes after the surface is restored: many times, at any point of a frame; a restore
//      asked for before the surface was destroyed again is dropped, the next one restores; a surface release after
//      a reset waits until a frame applied it (never a later read-back's flush frame, to a destroyed window);
//   4. the renderer shuts down and starts again.
#include "device.hpp"
#include "storm/render/frame_owner.hpp"

#include "check_backend.hpp"

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
        const shim::DeviceInfo &device = context.device_info();
        ok &= expect(!device.renderer.empty() && device.renderer != "?" &&
                         (device.vulkan ? device.version.find("Vulkan") != std::string::npos
                                        : device.version.find("OpenGL ES") != std::string::npos && !device.extensions.empty()),
                     mode, "the device's strings are known after init");
        // The self-test that decides whether a device keeps Vulkan (Context::start) holds on every backend.
        std::string failure;
        ok &= expect(context.backend_self_test(&failure), mode, "the backend self-test passes");
        if (!failure.empty())
            std::printf("  %s\n", failure.c_str());
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

        // surfaceChanged, then surfaceDestroyed again before the game thread took the restore (fold, split screen,
        // lock/unlock): the window SDL still hands out is the one being destroyed, so that restore is dropped; the
        // next surfaceChanged restores.
        context.request_surface_release();
        present(context, 2);
        context.request_surface_restore();
        context.request_surface_release();
        present(context, 3);
        ok &= expect(!context.ready(), mode, "a restore asked for before the surface was destroyed again is dropped");
        context.request_surface_restore();
        present(context, 3);
        ok &= expect(context.ready(), mode, "the next surfaceChanged restores");

        // A reset (here a resize; a restore or new reset flags alike) is applied by the next frame the render thread
        // runs, a read-back's flush frame too. A surfaceDestroyed right after it must not return before a frame
        // applied it to the window: Android destroys the window then, and a later read-back would apply the reset to
        // the destroyed one.
        {
            present(context, 2);
            context.resize(kWidth + 16, kHeight + 8);
            std::atomic<bool> returned{false}, pending_at_return{true};
            std::thread destroyed([&] {
                context.request_surface_release();
                pending_at_return = context.surface_reset_pending();
                returned = true;
            });
            // Android's UI thread first: the game thread's next frame comes only after a while.
            for (int wait = 0; wait < 200 && !returned; ++wait)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            for (int frame = 0; frame < 200 && !returned; ++frame)
                present(context, 1);
            destroyed.join();
            ok &= expect(!pending_at_return, mode,
                         "a surfaceDestroyed after a reset returns only once a frame applied the reset to the window");
            present(context, 2);
            ok &= expect(!context.ready() && !context.surface_reset_pending(), mode,
                         "the surface is given up with no reset left for a read-back's flush frame");
            context.request_surface_restore();
            present(context, 3);
            ok &= expect(context.ready(), mode, "... and restored");
        }
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
    host_check::UseBackendFromEnvironment();
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
