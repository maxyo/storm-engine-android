#include "device.hpp"
#include "effects/effects.hpp"
#include "frame_pacing.hpp"
#include "perf.hpp"
#include "profiler.hpp"
#include "program_cache.hpp"
#include "storm/render/d3d_interop.hpp"
#include "storm/render/log.hpp"
#include "storm/render/shader_blob.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

namespace bgfx
{
// patches/bgfx-render-thread-hooks.patch (src/bgfx.cpp) defines these two.
typedef void (*RenderFrameDoneFn)(uint32_t _frame);
void setRenderFrameDone(RenderFrameDoneFn _done);
// patches/bgfx-vk-unbound-samplers.patch (src/renderer_vk.cpp).
void setVkUnboundTextures(TextureHandle _texture2d, TextureHandle _textureCube, TextureHandle _texture3d);
} // namespace bgfx

namespace storm::shim
{

namespace
{
std::mutex trap_mutex;
std::map<std::string, uint64_t> trap_counts;

std::atomic<storm::render::LogSink> log_sink{nullptr};

double now_seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

void logf(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (const storm::render::LogSink sink = log_sink.load())
    {
        sink(std::strncmp(buffer, "ERROR", 5) == 0, buffer);
        return;
    }
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "storm-bgfx", "%s", buffer);
#endif
    fprintf(stderr, "[storm-bgfx] %s\n", buffer);
}

void logd(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_DEBUG, "storm-bgfx", "%s", buffer);
#endif
    fprintf(stderr, "[storm-bgfx] debug: %s\n", buffer);
}

HRESULT trap(const char *method)
{
    std::lock_guard lock(trap_mutex);
    if (++trap_counts[method] == 1)
        logf("TRAP: %s", method);
    return D3DERR_INVALIDCALL;
}

void dump_traps()
{
    std::lock_guard lock(trap_mutex);
    for (const auto &[name, count] : trap_counts)
        logf("trap %s x%llu", name.c_str(), static_cast<unsigned long long>(count));
}

Context &Context::instance()
{
    static Context context;
    return context;
}

} // namespace storm::shim

namespace storm::render
{
FrameOwner *FrameOwner::Get()
{
    auto &context = shim::Context::instance();
    return context.initialised() ? &context : nullptr;
}

uint16_t BgfxTextureOf(shim::BaseTexture *texture, uint16_t *width, uint16_t *height)
{
    shim::TexStore *store = shim::store_of(texture);
    if (!store || !shim::Context::instance().initialised())
        return UINT16_MAX;
    store->sync();
    store->settle();
    if (width)
        *width = static_cast<uint16_t>(store->width);
    if (height)
        *height = static_cast<uint16_t>(store->height);
    return store->handle.idx;
}

void MarkTextureImmutable(shim::BaseTexture *texture)
{
    shim::TexStore *store = shim::store_of(texture);
    if (!store)
        return;
    store->immutable = true;
    if (bgfx::isValid(store->handle) && !store->any_dirty)
        store->release_shadow(); // already uploaded
}

void SetGamePictureProvider(GamePictureProvider *provider)
{
    shim::Context::instance().set_picture_provider(provider);
}

#ifdef __ANDROID__
} // namespace storm::render

// Called by the activity on the UI thread right before SDL destroys the surface.
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeSurfaceDestroying(JNIEnv *, jclass)
{
    storm::shim::Context::instance().request_surface_release();
}

// Called by the activity on the UI thread when Android hands over a (new) surface: a surface that
// was destroyed while the app stayed in the foreground is restored without a background cycle.
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeSurfaceChanged(JNIEnv *, jclass)
{
    storm::shim::Context::instance().request_surface_restore();
}

// Called by the activity on the UI thread when the display density changes (wm density, display
// size setting, foldables) without a necessary change of the surface size.
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeDensityChanged(JNIEnv *, jclass)
{
    storm::shim::Context::instance().request_density_refresh();
}

namespace storm::render
{
#endif

void NotifySurfaceLost()
{
    shim::Context::instance().surface_lost();
}

void NotifySurfaceRestored()
{
    shim::Context::instance().surface_restored();
}

PictureRect GamePictureRect()
{
    return shim::Context::instance().picture_rect();
}

void SetRenderThread(bool enabled)
{
    shim::Context::instance().set_render_thread(enabled);
}

void SetRenderBackend(Backend backend, const std::filesystem::path &state_directory)
{
    shim::Context::instance().set_backend(backend, state_directory);
}

void SetVulkanAsyncPipelines(bool enabled)
{
    shim::Context::instance().set_async_pipelines(enabled);
}

const char *RenderBackendChoice()
{
    return shim::Context::instance().backend_choice().c_str();
}

void SetFrameOverlap(int mode)
{
    shim::Context::instance().set_frame_overlap(mode);
}

void HoldGamePicture(bool hold)
{
    shim::Context::instance().hold_picture(hold);
}

bool GamePictureHeld()
{
    return shim::Context::instance().picture_held();
}
} // namespace storm::render

namespace storm::shim
{

namespace
{
bool native_handles(SDL_Window *window, void **nwh, void **ndt)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info))
    {
        logf("SDL_GetWindowWMInfo failed: %s", SDL_GetError());
        return false;
    }
    *ndt = nullptr;
#if defined(__ANDROID__)
    *nwh = info.info.android.window;
#elif defined(SDL_VIDEO_DRIVER_X11)
    *nwh = reinterpret_cast<void *>(static_cast<uintptr_t>(info.info.x11.window));
    *ndt = info.info.x11.display;
#else
    *nwh = nullptr;
#endif
    return *nwh != nullptr;
}
} // namespace

bool DeviceInfo::has_extension(const char *name) const
{
    const size_t length = strlen(name);
    for (size_t at = extensions.find(name); at != std::string::npos; at = extensions.find(name, at + length))
        if ((at == 0 || extensions[at - 1] == ' ') && (at + length == extensions.size() || extensions[at + length] == ' '))
            return true;
    return false;
}

void Context::set_render_thread(bool enabled)
{
    if (initialised_ && enabled != render_thread_)
        logf("ERROR: render_thread=%d is set while the device exists; it applies to the next device", enabled);
    render_thread_ = enabled;
}

void Context::set_backend(render::Backend backend, std::filesystem::path state_directory)
{
    if (initialised_ && backend != backend_request_)
        logf("ERROR: render_backend is set while the device exists; it applies to the next device");
    backend_request_ = backend;
    backend_state_directory_ = std::move(state_directory);
}

void Context::set_async_pipelines(bool enabled)
{
    if (enabled != async_pipelines_)
        logf(enabled ? "pipelines: scene draws wait for new Vulkan pipelines off the render thread"
                     : "pipelines: vulkan_async_pipelines=0: every Vulkan pipeline is created on the render thread, at its "
                       "first draw");
    async_pipelines_ = enabled;
}

void Context::set_frame_overlap(int mode)
{
    overlap_mode_ = std::clamp(mode, -1, 1);
    overlap_logged_ = false;
}

std::chrono::nanoseconds Context::frame_gate(uint32_t number, std::chrono::nanoseconds game, std::chrono::nanoseconds blocked)
{
    using Ms = std::chrono::duration<double, std::milli>;
    std::unique_lock lock(release_mutex_);
    const std::chrono::nanoseconds render = render_work_;
    const std::chrono::nanoseconds interval = FramePacing::instance().preferred_interval();
    bool changed = !overlap_logged_;
    if (overlap_mode_ < 0)
        changed |= overlap_.update(Ms(game).count(), Ms(render).count(), Ms(interval).count());
    const bool overlapped = overlap_mode_ < 0 ? overlap_.overlapped() : overlap_mode_ == 1;
    if (changed)
    {
        overlap_logged_ = true;
        char why[160];
        if (overlap_mode_ >= 0)
            snprintf(why, sizeof(why), "render_overlap=%d", overlap_mode_);
        else if (interval.count() <= 0)
            snprintf(why, sizeof(why), "no frame rate to fit");
        else
            snprintf(why, sizeof(why), "game %.1f ms + render %.1f ms, %.0f%% of the %.2f ms frame smoothed", Ms(game).count(),
                     Ms(render).count(), 100.0 * overlap_.load(), Ms(interval).count());
        logf(overlapped ? "render thread: frames overlap (%s): the next frame is simulated while this one is rendered, one "
                          "frame more from input to picture"
                        : "render thread: frames take turns (%s): a frame is simulated after the previous one is presented, "
                          "no added latency",
             why);
    }
    const uint32_t awaited = number - (overlapped ? 1 : 0);
    const auto started = std::chrono::steady_clock::now();
    {
        STORM_PROFILE_ZONE("frame gate: the render thread presents");
        // Bounded: pacing only. A frame that takes longer is still waited for by the next bgfx::frame().
        release_done_.wait_for(lock, std::chrono::milliseconds(500),
                               [&] { return static_cast<int32_t>(done_frame_ - awaited) >= 0; });
    }
    const std::chrono::nanoseconds waited = blocked + (std::chrono::steady_clock::now() - started);
    // What the frame needs from the two threads together: both works in turn, or the longer one when they overlap.
    const std::chrono::nanoseconds also_work = overlapped ? std::max(render - game, std::chrono::nanoseconds::zero()) : render;
    return std::max(waited - also_work, std::chrono::nanoseconds::zero());
}

void Context::render_frame_done(uint32_t frame)
{
    Context &context = instance();
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::nanoseconds present_wait = FramePacing::instance().take_present_wait();
    DeviceInfo info;
    // The first frame bgfx::init runs creates the device (OpenGL ES: the GL context); start_backend() waits for
    // what is read here. Only the GL backend has a GL context to ask.
    const bool first = !context.render_info_read_ && (!context.gl_backend_ || glGetString(GL_VERSION));
    if (first && context.gl_backend_)
    {
        const auto text = [](GLenum name) {
            const auto *value = reinterpret_cast<const char *>(glGetString(name));
            return std::string(value ? value : "?");
        };
        info.vendor = text(GL_VENDOR);
        info.renderer = text(GL_RENDERER);
        info.version = text(GL_VERSION);
        info.extensions = text(GL_EXTENSIONS);
        info.anisotropy = info.has_extension("GL_EXT_texture_filter_anisotropic");
        if (info.anisotropy)
        {
            constexpr GLenum kMaxAnisotropy = 0x84FF; // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
            glGetFloatv(kMaxAnisotropy, &info.max_anisotropy);
        }
        glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &info.vertex_texture_units);
    }
    {
        std::lock_guard lock(context.release_mutex_);
        if (first)
        {
            context.render_gl_info_ = std::move(info);
            context.render_info_read_ = true;
            context.render_tid_ = static_cast<int>(gettid());
        }
        // The frame's work began when it was handed over, or when the frame before it was finished.
        const auto began = std::max(context.done_time_, context.hand_time_[frame & 3]);
        context.render_work_ = std::max<std::chrono::nanoseconds>(now - began - present_wait, std::chrono::nanoseconds::zero());
        context.done_time_ = now;
        context.done_frame_ = frame;
    }
    context.release_done_.notify_all();
}

bool Context::wait_render_idle(std::unique_lock<std::mutex> &lock, const char *what)
{
    // One frame can take a second on slow GPUs (the emulator), and its present waits for a vsync.
    if (release_done_.wait_for(lock, std::chrono::seconds(3), [this] { return render_idle(); }))
        return true;
    logf("ERROR: the render thread has not finished frame %u (last finished: %u) after 3 s: %s", handed_frame_, done_frame_,
         what);
    return false;
}

bool Context::init(SDL_Window *window, uint32_t width, uint32_t height, bool vsync)
{
    if (!window)
    {
        logf("ERROR: bgfx needs a window");
        return false;
    }
    return start(window, width, height, vsync);
}

bool Context::init_headless(uint32_t width, uint32_t height)
{
    return start(nullptr, width, height, false);
}

bool Context::start(SDL_Window *window, uint32_t width, uint32_t height, bool vsync)
{
    if (initialised_)
    {
        resize(width, height);
        return true;
    }
    window_ = window;
    vsync_ = vsync;
    backend_attempt_.reset();
    backend_presented_ = 0;
    std::string failure;
    if (!window)
    {
        // init_headless(): the shim does all its work, bgfx's Noop backend draws nothing.
        logf("headless: bgfx Noop renderer, nothing is drawn");
        backend_choice_ = "none (headless: bgfx Noop)";
        if (!start_backend(bgfx::RendererType::Noop, nullptr, width, height, &failure))
        {
            logf("ERROR: %s", failure.c_str());
            return false;
        }
    }
    else
    {
        const VulkanDevice vulkan = ProbeVulkan();
        BackendAttempt attempt(backend_state_directory_, vulkan);
        BackendDecision decision = DecideBackend(backend_request_, vulkan, attempt.failed_before());
        if (decision.vulkan)
        {
            // The marker stays until frames were presented (present()); a start that fails here in an orderly way
            // leaves it too, so the next starts go to OpenGL ES at once instead of trying Vulkan every time.
            attempt.begin();
            if (start_backend(bgfx::RendererType::Vulkan, &vulkan, width, height, &failure) && !backend_self_test(&failure))
                stop_backend();
            if (failure.empty())
                backend_attempt_.emplace(std::move(attempt));
            else
            {
                // The explicit exception to "no fallbacks": a device whose Vulkan cannot run the game still has to
                // start. Said as an error, with the reason, and kept in the choice line below.
                logf("ERROR: render backend: Vulkan failed on %s: %s. Starting on OpenGL ES; Vulkan is not tried again "
                     "on this driver until the cache is cleared",
                     vulkan.describe().c_str(), failure.c_str());
                decision = {false, "Vulkan failed (" + failure + ")"};
                failure.clear();
            }
        }
        if (!decision.vulkan && !start_backend(bgfx::RendererType::OpenGLES, nullptr, width, height, &failure))
        {
            logf("ERROR: %s", failure.c_str());
            return false;
        }
        static const char *const requests[] = {"auto", "gles", "vulkan"};
        backend_choice_ = std::string(decision.vulkan ? "Vulkan" : "OpenGL ES") + " (render_backend = " +
                          requests[static_cast<int>(backend_request_)] + "): " + decision.reason + "; Vulkan device: " +
                          vulkan.describe();
        // An asked-for backend that is not the one running is an error line; either way the same text.
        const bool refused = (backend_request_ == render::Backend::Vulkan) != decision.vulkan &&
                             backend_request_ != render::Backend::Auto;
        logf("%srender backend: %s", refused ? "ERROR: " : "", backend_choice_.c_str());
        if (decision.vulkan && !vulkan.large_points)
            logf("render backend: the Vulkan device has no largePoints: point lists (the stars) are one pixel wide");
    }
    initialised_ = true;
    surface_.width_px = width;
    surface_.height_px = height;
    surface_.valid = true;
    surface_in_use_ = true;
    if (window_)
        read_density();
    ++surface_.generation;
    logf("bgfx renderer=%s %ux%u originBottomLeft=%d homogeneousDepth=%d maxTextureSize=%u BC1=%d BC3=%d ETC2=%d BGRA8=%d "
         "border_clamp=%d border_colors=%d timer_queries=%s",
         bgfx::getRendererName(bgfx::getRendererType()), width, height, caps_->originBottomLeft,
         caps_->homogeneousDepth, caps_->limits.maxTextureSize,
         (caps_->formats[bgfx::TextureFormat::BC1] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
         (caps_->formats[bgfx::TextureFormat::BC3] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
         (caps_->formats[bgfx::TextureFormat::ETC2] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
         (caps_->formats[bgfx::TextureFormat::BGRA8] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0, border_clamp_, border_colors_,
         timer_queries_);
    effects::Host::instance().on_device_created();
    for (auto &entry : clients_)
    {
        entry.client->OnDeviceRestored();
        entry.client->OnSurfaceChanged(surface_);
    }
    return true;
}

bool Context::start_backend(bgfx::RendererType::Enum type, const VulkanDevice *vulkan, uint32_t width, uint32_t height,
                            std::string *failure)
{
    const bool window = type != bgfx::RendererType::Noop;
    gl_backend_ = type == bgfx::RendererType::OpenGLES;
    vulkan_backend_ = type == bgfx::RendererType::Vulkan;
    device_info_ = {};
    {
        std::lock_guard lock(release_mutex_);
        frames_handed_ = false;
        render_info_read_ = false;
    }
    bgfx::Init init;
    init.type = type;
    if (window)
    {
        if (!native_handles(window_, &init.swapChain.nwh, &init.swapChain.ndt))
        {
            *failure = "the window has no native handle";
            return false;
        }
        FramePacing::instance().start(render_thread_, vulkan != nullptr); // bgfx presents through it from the first frame
    }
    if (vulkan)
    {
        // The device the probe read is the one bgfx takes: it matches these ids where they fit bgfx's 16 bits (a
        // software device goes by bgfx's own vendor id; an Adreno's device id is wider and left open) and otherwise
        // prefers devices in the probe's order. Checked below.
        init.vendorId = vulkan->software ? BGFX_PCI_ID_SOFTWARE_RASTERIZER : static_cast<uint16_t>(vulkan->vendor_id);
        init.deviceId = vulkan->device_id <= UINT16_MAX ? static_cast<uint16_t>(vulkan->device_id) : 0;
        // The pipeline cache is valid for this device, driver and bgfx version only (the driver checks its own header
        // too). Before bgfx::init, which loads it.
        ProgramCache::instance().attach_device(vulkan->cache_key() + " | one cache | bgfx API " + std::to_string(BGFX_API_VERSION), true);
    }
    init.swapChain.width = width;
    init.swapChain.height = height;
    // No depth/stencil on the window surface: nothing draws into the back buffer with depth or stencil (the game and the
    // UI have their own targets; the composite and the UI's final pass write colour only). Spares a tile GPU the
    // surface's depth/stencil memory and traffic.
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::Count;
    init.reset = reset_flags();
    // Transient buffers sized to the largest frames measured (scene sweeps, perf counters tvb_max_kb/tib_max_kb: 10.2 MB
    // of vertices, 4.9 MB of indices) with a margin; each new peak is logged ("transient buffers:", perf.cpp) and a
    // draw that does not fit is a logged TRAP. The GL backend grows its buffers to what frames really write
    // (patches/bgfx-gl-transient-ring.patch).
    init.limits.maxTransientVbSize = 16u << 20;
    init.limits.maxTransientIbSize = 8u << 20;
    init.fallback = false; // never continue on another renderer than the one chosen here
    init.callback = &ProgramCache::instance(); // program binaries between runs; bgfx fatals and traces into the log
    bgfx::setRenderFrameDone(&Context::render_frame_done);
    // bgfx's own switch: a renderFrame() call before init makes the calling thread the render thread, and no
    // thread is created (the backend then runs inside bgfx::frame()).
    if (!render_thread_)
        bgfx::renderFrame();
    if (!bgfx::init(init))
    {
        bgfx::setRenderFrameDone(nullptr);
        FramePacing::instance().shutdown();
        ProgramCache::instance().detach_device();
        *failure = std::string("bgfx::init failed on ") + bgfx::getRendererName(type);
        return false;
    }
    caps_ = bgfx::getCaps();
    render_thread_active_ = (caps_->supported & BGFX_CAPS_RENDERER_MULTITHREADED) != 0;
    if (render_thread_active_ != render_thread_)
        logf("ERROR: render thread: asked for %s, bgfx runs %s", render_thread_ ? "on" : "off",
             render_thread_active_ ? "multithreaded" : "single-threaded");
    else if (render_thread_active_)
        logf("render thread: on (bgfx multithreaded: the backend, the driver and the present run on bgfx's render "
             "thread)");
    else
        logf("render thread: OFF (render_thread=0): bgfx renders and presents inside bgfx::frame() on the game thread");
    if (window)
    {
        // The render thread reports its id (and on OpenGL ES reads the driver's strings) after the first frame of
        // bgfx::init, which may still be on its way.
        std::unique_lock lock(release_mutex_);
        if (!release_done_.wait_for(lock, std::chrono::seconds(5), [this] { return render_info_read_; }))
        {
            lock.unlock();
            stop_backend();
            *failure = "the render thread reported no device within 5 s of bgfx::init";
            return false;
        }
        device_info_ = render_gl_info_;
    }
    if (window)
        FramePacing::instance().set_window(init.swapChain.nwh);
    if (window && render_thread_active_)
        FramePacing::instance().render_thread_started(render_tid_);
    flip_y_ = caps_->originBottomLeft;
    border_clamp_ = border_colors_ = false;
    if (vulkan)
    {
        // bgfx reports the low 16 bits of the device's ids.
        if (caps_->vendorId != static_cast<uint16_t>(vulkan->vendor_id) ||
            caps_->deviceId != static_cast<uint16_t>(vulkan->device_id))
        {
            char ids[96];
            snprintf(ids, sizeof(ids), "vendor 0x%x device 0x%x, not the probed device", caps_->vendorId, caps_->deviceId);
            stop_backend();
            *failure = std::string("bgfx took another Vulkan device (") + ids + ")";
            return false;
        }
        char text[64];
        device_info_.vulkan = true;
        snprintf(text, sizeof(text), "vendor 0x%x", vulkan->vendor_id);
        device_info_.vendor = text;
        device_info_.renderer = vulkan->name;
        snprintf(text, sizeof(text), "Vulkan %u.%u.%u, driver 0x%x", (vulkan->api_version >> 22) & 0x7f,
                 (vulkan->api_version >> 12) & 0x3ff, vulkan->api_version & 0xfff, vulkan->driver_version);
        device_info_.version = text;
        device_info_.anisotropy = vulkan->anisotropy;
        device_info_.max_anisotropy = vulkan->max_anisotropy;
        device_info_.vertex_texture_units = vulkan->vertex_texture_units;
        border_clamp_ = true; // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER is core
        border_colors_ = vulkan->border_color;
        timer_queries_ = "Vulkan timestamp queries"; // bgfx's TimerQueryVK, where the queue has timestampValidBits
    }
    else if (window)
    {
        // The program binaries are valid for this GPU, driver and bgfx version only.
        ProgramCache::instance().attach_device(device_info_.vendor + " | " + device_info_.renderer + " | " +
                                                   device_info_.version + " | bgfx API " + std::to_string(BGFX_API_VERSION),
                                               false);
        // The extensions bgfx itself requires before it uses GL_CLAMP_TO_BORDER (renderer_gl.cpp).
        border_clamp_ = border_colors_ = device_info_.has_extension("GL_EXT_texture_border_clamp") ||
                                         device_info_.has_extension("GL_OES_texture_border_clamp") ||
                                         device_info_.has_extension("GL_NV_texture_border_clamp");
        // GPU timers (effects profile gpu_*_ms, Tracy): GLES offers them only through this extension
        // (patches/bgfx-gles-timer-queries.patch imports its entry points).
        timer_queries_ = device_info_.has_extension("GL_EXT_disjoint_timer_query") ? "EXT_disjoint_timer_query" : "none";
    }
    else
        timer_queries_ = "none (headless)";
    create_resources();
    return true;
}

void Context::stop_backend()
{
    bgfx::shutdown(); // joins the render thread; every bgfx handle is gone with it
    bgfx::setRenderFrameDone(nullptr);
    if (vulkan_backend_)
        bgfx::setVkUnboundTextures(BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    {
        // The frames bgfx::shutdown ran itself were never handed over by run_frame.
        std::lock_guard lock(release_mutex_);
        frames_handed_ = false;
    }
    render_thread_active_ = false;
    FramePacing::instance().shutdown();
    ProgramCache::instance().detach_device();
    caps_ = nullptr;
}

bool Context::backend_self_test(std::string *failure)
{
    // A 2x2 texture, one colour per texel, drawn over a 16x16 target with the blit program (point sampled, the
    // texture's top row at the target's top as every blit of the shim), copied into a read-back texture and read.
    constexpr uint16_t kSize = 16;
    static const uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    constexpr uint64_t kPoint = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT |
                                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    const bgfx::TextureHandle source =
        bgfx::createTexture2D(2, 2, false, 1, bgfx::TextureFormat::RGBA8, kPoint, bgfx::makeRef(texels, sizeof(texels)));
    const bgfx::TextureHandle target =
        bgfx::createTexture2D(kSize, kSize, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | kPoint);
    const bgfx::TextureHandle staging = bgfx::createTexture2D(kSize, kSize, false, 1, bgfx::TextureFormat::RGBA8,
                                                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | kPoint);
    const bgfx::FrameBufferHandle frame_buffer = bgfx::createFrameBuffer(1, &target, false);
    std::vector<uint8_t> pixels(size_t(kSize) * kSize * 4, 0);
    const bool created = bgfx::isValid(source) && bgfx::isValid(target) && bgfx::isValid(staging) &&
                         bgfx::isValid(frame_buffer) && bgfx::isValid(blit_program_);
    if (created)
    {
        constexpr bgfx::ViewId kDraw = 0, kCopy = 1;
        bgfx::setViewFrameBuffer(kDraw, frame_buffer);
        bgfx::setViewRect(kDraw, 0, 0, kSize, kSize);
        bgfx::setViewClear(kDraw, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        // As Device::StretchRect draws into a target: v runs with the target's rows where the origin is at the top.
        if (flip_y_)
            draw_fullscreen(kDraw, source, static_cast<uint32_t>(kPoint), BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0, 1, 1, 0);
        else
            draw_fullscreen(kDraw, source, static_cast<uint32_t>(kPoint), BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::TextureRegion from, to;
        from.init(target);
        to.init(staging);
        bgfx::blit(kCopy, to, from);
        const uint32_t ready = bgfx::read(to, pixels.data());
        uint32_t current = 0;
        while (current < ready)
            run_frame(BGFX_FRAME_FLUSH, &current); // presents nothing; `pixels` is written before it returns `ready`
        bgfx::setViewFrameBuffer(kDraw, BGFX_INVALID_HANDLE);
    }
    for (const bgfx::TextureHandle texture : {source, target, staging})
        if (bgfx::isValid(texture))
            bgfx::destroy(texture);
    if (bgfx::isValid(frame_buffer))
        bgfx::destroy(frame_buffer);
    if (!created)
    {
        *failure = "the backend self-test could not create its textures, target or program";
        return false;
    }
    // The centre of each quadrant: the read-back's first row is the target's top row on a top-left origin backend
    // and its bottom row on a bottom-left one; the picture was drawn accordingly, so the rows read the same.
    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        const int x = quadrant % 2 ? 12 : 4, y = quadrant / 2 ? 12 : 4;
        const uint8_t *got = &pixels[(size_t(y) * kSize + x) * 4];
        const uint8_t *want = &texels[quadrant * 4];
        if (std::abs(got[0] - want[0]) > 2 || std::abs(got[1] - want[1]) > 2 || std::abs(got[2] - want[2]) > 2)
        {
            char text[192];
            snprintf(text, sizeof(text),
                     "the backend self-test read %u,%u,%u at (%d, %d) of its known picture, expected %u,%u,%u", got[0],
                     got[1], got[2], x, y, want[0], want[1], want[2]);
            *failure = text;
            return false;
        }
    }
    logf("render backend: self-test passed on %s (shaders, a pipeline, a texture, a target and its read-back)",
         bgfx::getRendererName(bgfx::getRendererType()));
    return true;
}

void Context::create_resources()
{
    static const char *names[kSamplerUnits] = {"s_tex0", "s_tex1", "s_tex2", "s_tex3", "s_tex4", "s_cube0",
                                                "s_cube1", "s_cube2", "s_vol0", "s_vol1", "s_vol2",
                                                // pixel-port extras: cube at stage 3, volume at stage 4 (sea)
                                                "s_cube3", "s_vol4",
                                                // skinned fixed-function draws (ff_vertex.sh FF_SKIN)
                                                "s_bones"};
    for (uint32_t i = 0; i < kSamplerUnits; ++i)
        samplers_[i] = bgfx::createUniform(names[i], bgfx::UniformType::Sampler);
    blit_program_ = bgfx::createProgram(create_embedded_shader(shader_id_by_name("vs_blit")),
                                        create_embedded_shader(shader_id_by_name("fs_blit")), true);
    blit_layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    // One oversized triangle in a static buffer (the composite runs after the scene has used the
    // transient budget); texcoord holds the corner factor, u_blitUv maps it to the requested range.
    static const float vertices[12] = {-1.0f, 1.0f, 0.0f, 0.0f, 3.0f, 1.0f, 2.0f, 0.0f, -1.0f, -3.0f, 0.0f, 2.0f};
    blit_vertices_ = bgfx::createVertexBuffer(bgfx::makeRef(vertices, sizeof(vertices)), blit_layout_);
    blit_uv_ = bgfx::createUniform("u_blitUv", bgfx::UniformType::Vec4);
    if (vulkan_backend_)
    {
        // What a sampler samples when a draw sets no texture for it: black, as on OpenGL ES, where the draws rely on
        // it (a fixed-function program declares five stages, a draw binds those its material uses). Vulkan needs a
        // texture there (patches/bgfx-vk-unbound-samplers.patch). They go with bgfx::shutdown.
        static const uint8_t black[6 * 4] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
        constexpr uint64_t kPoint = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
        bgfx::setVkUnboundTextures(
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, kPoint, bgfx::makeRef(black, 4)),
            bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, kPoint, bgfx::makeRef(black, sizeof(black))),
            bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::RGBA8, kPoint, bgfx::makeRef(black, 4)));
    }
}

void Context::shutdown()
{
    if (!initialised_)
        return;
    for (auto &entry : clients_)
        entry.client->OnDeviceLost();
    dump_traps();
    effects::Host::instance().shutdown();
    bone_palettes_.shutdown();
    stop_backend();
    overlap_ = {};
    overlap_logged_ = false;
    gate_end_ = {};
    backend_attempt_.reset(); // a Vulkan start that never presented keeps its marker
    initialised_ = false;
    surface_.valid = false;
    surface_lost();
    pictures_.clear();
    picture_provider_ = nullptr;
}

void Context::resize(uint32_t width, uint32_t height)
{
    // While the surface is gone the new size is picked up by surface_restored().
    if (!initialised_ || !surface_.valid || (width == surface_.width_px && height == surface_.height_px))
        return;
    surface_.width_px = width;
    surface_.height_px = height;
    ++surface_.generation;
    bgfx::SwapChain chain;
    native_handles(window_, &chain.nwh, &chain.ndt);
    chain.width = width;
    chain.height = height;
    chain.formatDepthStencil = bgfx::TextureFormat::Count; // as in start()
    bgfx::reset(reset_flags(), &chain);
    picture_held_ = false; // see surface_lost
    // A display change (fold, split screen, wm size/density) usually changes the density as well.
    read_density();
    logf("surface resized to %ux%u, density %.2f", width, height, surface_.density);
    for (auto &entry : clients_)
        entry.client->OnSurfaceChanged(surface_);
}

void Context::refresh_reset_flags()
{
    if (!initialised_ || !surface_.valid)
        return;
    bgfx::SwapChain chain;
    native_handles(window_, &chain.nwh, &chain.ndt);
    chain.width = surface_.width_px;
    chain.height = surface_.height_px;
    chain.formatDepthStencil = bgfx::TextureFormat::Count; // as in start()
    bgfx::reset(reset_flags(), &chain);
    picture_held_ = false; // see surface_lost
}

bool Context::read_density()
{
    float dpi = 0.0f;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window_), &dpi, nullptr, nullptr) != 0 || dpi <= 0.0f)
    {
        logf("display density unknown to SDL (%s); keeping %.2f", SDL_GetError(), surface_.density);
        return false;
    }
    if (dpi / 160.0f == surface_.density)
        return false;
    surface_.density = dpi / 160.0f;
    return true;
}

void Context::request_density_refresh()
{
    density_refresh_requested_ = true;
}

void Context::request_surface_restore()
{
    std::lock_guard lock(release_mutex_);
    restore_destroys_ = surface_destroys_;
    restore_requested_ = true;
}

uint32_t Context::reset_flags() const
{
    // Flip right after rendering: no swap of a previous frame is ever pending when the surface changes.
    // Effects: anisotropic filtering of world textures at the level of fx::RendererState (before the device exists this
    // is the request; without the extension the backend ignores the flag).
    const bool anisotropy = effects::Host::instance().renderer_state().max_anisotropy > 1;
#ifdef __ANDROID__
    // Android Frame Pacing on Vulkan paces a FIFO swap chain (frame_pacing.hpp), which is bgfx's vsync mode.
    const bool vsync = vsync_ || vulkan_backend_;
#else
    const bool vsync = vsync_;
#endif
    return (vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE) | BGFX_RESET_FLIP_AFTER_RENDER |
           (anisotropy ? BGFX_RESET_MAXANISOTROPY : BGFX_RESET_NONE) |
           (surface_epoch_flag_ ? BGFX_RESET_FLUSH_AFTER_RENDER : BGFX_RESET_NONE);
}

void Context::surface_lost()
{
    if (surface_.valid)
    {
        surface_.valid = false;
        ++surface_.generation;
        frame_aborted_ = frame_open_;
        picture_held_ = false; // the kept pause picture may not survive the surface: drawn again and kept anew
        logf("surface lost: presentation suspended");
    }
    {
        std::unique_lock lock(release_mutex_);
        // The render thread may still be presenting the last frame to the window.
        wait_render_idle(lock, "Swappy's window is taken away and the surface given up while a frame is presented");
        surface_in_use_ = false;
        release_requested_ = false;
    }
    FramePacing::instance().set_window(nullptr); // Swappy lets go of the window Android destroys
    release_done_.notify_all();
}

void Context::request_surface_release()
{
    std::unique_lock lock(release_mutex_);
    ++surface_destroys_; // a restore asked for before this one must not take the window SDL still hands out
    if (!surface_in_use_ && !restoring_)
        return;
    release_requested_ = true;
    // The EGL surface is touched by the frames handed to the render thread, until each is presented; no new
    // presenting frame starts once release_requested_ is set. With nothing in flight this returns at once.
    wait_render_idle(lock, "Android asked for the surface; it is destroyed while in use");
}

bool Context::run_frame(uint32_t flags, uint32_t *frame_number)
{
    // The frame's bone palettes go up in one update, before the draws that read them are rendered.
    bone_palettes_.flush();
    bool refused = false;
    {
        std::lock_guard lock(release_mutex_);
        // Nothing is presented to a surface that is gone or going (background, surfaceDestroyed).
        refused = !(flags & BGFX_FRAME_FLUSH) && !(surface_in_use_ && !release_requested_);
        in_frame_ = !refused;
    }
    if (refused)
    {
        surface_lost();
        return false;
    }
    const auto before = std::chrono::steady_clock::now();
    const uint32_t number = bgfx::frame(flags);
    frame_blocked_ = std::chrono::steady_clock::now() - before;
    ++submitted_frames_;
    {
        std::lock_guard lock(release_mutex_);
        in_frame_ = false;
        handed_frame_ = number;
        frames_handed_ = true;
        hand_time_[number & 3] = before;
    }
    release_done_.notify_all();
    if (frame_number)
        *frame_number = number;
    return true;
}

void Context::poll_surface()
{
    // A restore request is taken before the release request is looked at: the release that preceded it on
    // Android's thread is then seen too, whenever the two arrive (the other order dropped the restore when both
    // came between the two checks).
    const bool restore = restore_requested_.exchange(false);
    if (release_requested_.load())
        surface_lost();
    if (restore && initialised_ && !surface_.valid)
        restore_pending_ = true;
    if (restore_pending_)
        try_restore();
    // Without a surface the request stays pending; try_restore re-reads the density anyway.
    if (surface_.valid && density_refresh_requested_.exchange(false) && read_density())
    {
        ++surface_.generation;
        logf("display density %.2f", surface_.density);
        for (auto &entry : clients_)
            entry.client->OnSurfaceChanged(surface_);
    }
}

void Context::surface_restored()
{
    if (!initialised_ || surface_.valid)
        return;
    {
        std::lock_guard lock(release_mutex_);
        restore_destroys_ = surface_destroys_;
    }
    restore_pending_ = true;
    try_restore();
}

void Context::try_restore()
{
    bgfx::SwapChain chain;
    // From the read of the native window until bgfx and Swappy hold it, a surfaceDestroyed waits for this restore
    // (render_idle); one that came after the restore was asked for drops it below, before the window is used.
    auto window_released = [this] {
        {
            std::lock_guard lock(release_mutex_);
            restoring_ = false;
        }
        release_done_.notify_all();
    };
    {
        std::lock_guard lock(release_mutex_);
        restoring_ = true;
    }
    // SDL drops its native window when Android destroys the surface and holds a new one only after
    // surfaceCreated; until then there is nothing to attach to and the restore stays pending.
    if (!native_handles(window_, &chain.nwh, &chain.ndt))
    {
        window_released();
        if (!restore_wait_logged_)
            logf("surface restore is waiting for the new native window");
        restore_wait_logged_ = true;
        return;
    }
    restore_pending_ = false;
    restore_wait_logged_ = false;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (w > 0 && h > 0)
    {
        surface_.width_px = static_cast<uint32_t>(w);
        surface_.height_px = static_cast<uint32_t>(h);
    }
    // The display may have changed while the surface was gone (density included).
    density_refresh_requested_ = false;
    read_density();
    chain.width = surface_.width_px;
    chain.height = surface_.height_px;
    chain.formatDepthStencil = bgfx::TextureFormat::Count; // as in start()
    // A new ANativeWindow can reuse the old address; alternating this otherwise harmless flag makes
    // bgfx re-create the EGL surface on every restore.
    surface_epoch_flag_ = !surface_epoch_flag_;
    {
        std::unique_lock lock(release_mutex_);
        if (surface_destroys_ != restore_destroys_)
        {
            lock.unlock();
            window_released();
            logf("surface restore dropped: the surface was destroyed again after it was handed over (the window read "
                 "may be the destroyed one); the next surface restores");
            return;
        }
        // From here on a new surfaceDestroyed waits for our frames.
        surface_in_use_ = true;
    }
    bgfx::reset(reset_flags(), &chain);
    FramePacing::instance().set_window(chain.nwh);
    window_released();
    if (frame_aborted_)
    {
        // Draws submitted before the surface went away belong to a frame that was never finished.
        if (!run_frame(BGFX_FRAME_DISCARD))
            return; // the new surface is already going away again
        frame_aborted_ = false;
    }
    surface_.valid = true;
    ++surface_.generation;
    picture_held_ = false; // render targets drawn before the restore are not trusted: see surface_lost
    logf("surface restored %ux%u", surface_.width_px, surface_.height_px);
    for (auto &entry : clients_)
        entry.client->OnSurfaceChanged(surface_);
}

void Context::begin_frame_if_needed()
{
    if (!initialised_)
        return;
    poll_surface();
    if (frame_open_)
        return;
    frame_open_ = true;
    frame_.surface = surface_;
    for (auto &entry : clients_)
        entry.client->OnFrameBegin(frame_);
}

void Context::draw_fullscreen(uint16_t view, bgfx::TextureHandle texture, uint32_t sampler_flags, uint64_t state,
                              float u0, float v0, float u1, float v1)
{
    // (u0,v0) is the texture coordinate at clip (-1,+1), (u1,v1) at (+1,-1).
    const float uv[4] = {u0, v0, u1 - u0, v1 - v0};
    bgfx::setUniform(blit_uv_, uv);
    bgfx::setVertexBuffer(0, blit_vertices_);
    bgfx::setTexture(0, samplers_[0], texture, sampler_flags);
    bgfx::setState(state);
    bgfx::submit(view, blit_program_);
}

#ifdef STORM_TRACY
namespace
{
// Tracy plots of the presented frame: bgfx's own CPU and GPU timers and counters, and the process's resident memory.
void plot_frame_stats()
{
    const bgfx::Stats *stats = bgfx::getStats();
    if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
        STORM_PROFILE_PLOT("bgfx GPU frame ms", double(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 / stats->gpuTimerFreq);
    if (stats->cpuTimerFreq > 0)
        STORM_PROFILE_PLOT("bgfx CPU frame ms", double(stats->cpuTimeEnd - stats->cpuTimeBegin) * 1000.0 / stats->cpuTimerFreq);
    STORM_PROFILE_PLOT("bgfx draws", int64_t(stats->numDraw));
    if (stats->textureMemoryUsed >= 0)
        STORM_PROFILE_PLOT("bgfx texture MB", double(stats->textureMemoryUsed) / (1024.0 * 1024.0));
    if (stats->rtMemoryUsed >= 0)
        STORM_PROFILE_PLOT("bgfx render target MB", double(stats->rtMemoryUsed) / (1024.0 * 1024.0));
    STORM_PROFILE_PLOT("bgfx transient VB KB", double(stats->transientVbUsed) / 1024.0);
    static uint32_t frames = 0;
    if (frames++ % 30 == 0)
    {
        // Resident pages (statm field 2): twice a second at 60 fps.
        if (FILE *statm = fopen("/proc/self/statm", "r"))
        {
            unsigned long size = 0, resident = 0;
            if (fscanf(statm, "%lu %lu", &size, &resident) == 2)
                STORM_PROFILE_PLOT("resident MB", double(resident) * sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0));
            fclose(statm);
        }
    }
}
} // namespace
#endif

void Context::present(bgfx::TextureHandle game_color, uint32_t game_width, uint32_t game_height)
{
    STORM_PROFILE_ZONE("present");
    if (!initialised_)
        return;
    begin_frame_if_needed();
    presented_game_color_ = game_color;
    if (surface_.valid)
    {
        const uint16_t w = static_cast<uint16_t>(surface_.width_px), h = static_cast<uint16_t>(surface_.height_px);
        bgfx::setViewFrameBuffer(render::kCompositeView, BGFX_INVALID_HANDLE);
        bgfx::setViewMode(render::kCompositeView, bgfx::ViewMode::Sequential);
        bgfx::setViewRect(render::kCompositeView, 0, 0, w, h);
        bgfx::setViewClear(render::kCompositeView, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        bgfx::touch(render::kCompositeView);
        if (bgfx::isValid(game_color) && game_width && game_height)
        {
            // The game target keeps its size when the window is resized (split screen, foldables):
            // it is scaled to fit with its own aspect ratio, the rest of the surface stays black.
            const float scale = std::min(static_cast<float>(w) / game_width, static_cast<float>(h) / game_height);
            const uint16_t fw = static_cast<uint16_t>(std::lround(game_width * scale));
            const uint16_t fh = static_cast<uint16_t>(std::lround(game_height * scale));
            const uint16_t fx = static_cast<uint16_t>((w - fw) / 2), fy = static_cast<uint16_t>((h - fh) / 2);
            // Texture coordinates at the clip-space edges so that [0, 1] covers exactly the fitted rectangle.
            const float sx = static_cast<float>(w) / fw, sy = static_cast<float>(h) / fh;
            picture_rect_ = {static_cast<float>(fx) / w, static_cast<float>(fy) / h, static_cast<float>(fw) / w,
                             static_cast<float>(fh) / h};
            bgfx::setScissor(fx, fy, fw, fh);
            draw_fullscreen(render::kCompositeView, game_color, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0.5f - 0.5f * sx, 0.5f - 0.5f * sy,
                            0.5f + 0.5f * sx, 0.5f + 0.5f * sy);
        }
        for (uint16_t i = 0; i < render::kUiViewCount; ++i)
        {
            const uint16_t id = render::kUiViewBegin + i;
            bgfx::setViewFrameBuffer(id, BGFX_INVALID_HANDLE);
            bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
            bgfx::setViewRect(id, 0, 0, w, h);
            bgfx::setViewClear(id, BGFX_CLEAR_NONE);
        }
        {
            STORM_PROFILE_ZONE("UI render");
            for (auto &entry : clients_)
                entry.client->OnUiRender(frame_);
        }
        const auto frame_call = std::chrono::steady_clock::now();
        bool presented = false;
        {
            // With the render thread: the wait for it to finish rendering the previous frame, then the hand-over.
            // Without (render_thread=0): the GL backend, the driver and the present.
            STORM_PROFILE_ZONE("bgfx::frame");
            presented = run_frame(BGFX_FRAME_NONE, &last_frame_);
        }
        if (presented)
        {
            // A Vulkan start is confirmed by frames on the screen: its marker goes (backend_select.hpp).
            if (backend_attempt_ && ++backend_presented_ >= 3)
            {
                backend_attempt_->succeed();
                backend_attempt_.reset();
            }
            FramePacing::instance().game_frame();
            double wait_ms = 0.0;
            if (render_thread_active_)
            {
                const auto game = gate_end_ == std::chrono::steady_clock::time_point{} ? std::chrono::nanoseconds::zero()
                                                                                       : frame_call - gate_end_;
                FramePacing::instance().game_thread_waited(frame_gate(last_frame_, game, frame_blocked_));
                gate_end_ = std::chrono::steady_clock::now();
                wait_ms = std::chrono::duration<double, std::milli>(gate_end_ - frame_call).count();
            }
            perf_frame_presented(wait_ms);
            STORM_PROFILE_FRAME();
#ifdef STORM_TRACY
            plot_frame_stats();
#endif
        }
        ProgramCache::instance().log_activity();
    }
    else
    {
        // No surface (background, also in the middle of a scene load whose progress view keeps
        // calling Present): bgfx is not touched at all. Draws are refused while the surface is
        // invalid, so nothing accumulates; the frame simply resumes after surface_restored().
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (auto &entry : clients_)
        entry.client->OnFrameEnd(frame_);
    const double now = now_seconds();
    static double previous = now;
    frame_.delta_seconds = now - previous;
    previous = now;
    ++frame_.frame_index;
    frame_open_ = false;
}

void Context::AddClient(render::FrameClient *client, int order)
{
    if (!client)
        return;
    RemoveClient(client);
    auto it = std::upper_bound(clients_.begin(), clients_.end(), order,
                               [](int value, const Client &entry) { return value < entry.order; });
    clients_.insert(it, Client{client, order});
    if (initialised_)
    {
        client->OnDeviceRestored();
        client->OnSurfaceChanged(surface_);
    }
}

void Context::RemoveClient(render::FrameClient *client)
{
    std::erase_if(clients_, [client](const Client &entry) { return entry.client == client; });
}

namespace
{
bgfx::TextureFormat::Enum to_bgfx(render::PixelFormat format)
{
    switch (format)
    {
    case render::PixelFormat::RGBA8:
        return bgfx::TextureFormat::RGBA8;
    case render::PixelFormat::A8:
        return bgfx::TextureFormat::A8;
    case render::PixelFormat::ETC2_RGB8:
        return bgfx::TextureFormat::ETC2;
    case render::PixelFormat::ETC2_RGBA8:
        return bgfx::TextureFormat::ETC2A;
    case render::PixelFormat::ETC2_RGB8A1:
        return bgfx::TextureFormat::ETC2A1;
    }
    return bgfx::TextureFormat::Unknown;
}
} // namespace

bool Context::IsFormatSupported(render::PixelFormat format) const
{
    return initialised_ && (caps_->formats[to_bgfx(format)] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;
}

render::TextureId Context::CreateTexture(const render::TextureDesc &desc, std::span<const std::byte> pixels)
{
    if (!initialised_ || desc.width == 0 || desc.height == 0 || !IsFormatSupported(desc.format))
        return 0;
    uint64_t flags = 0;
    if (!(desc.flags & render::TextureFlags::Repeat))
        flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    if (desc.flags & render::TextureFlags::Point)
        flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    const bool updatable = desc.flags & render::TextureFlags::Updatable;
    const bgfx::Memory *memory = nullptr;
    if (!pixels.empty() && !updatable)
        memory = bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size()));
    const bgfx::TextureHandle handle =
        bgfx::createTexture2D(desc.width, desc.height, desc.mip_count > 1, 1, to_bgfx(desc.format), flags, memory);
    if (!bgfx::isValid(handle))
        return 0;
    if (!pixels.empty() && updatable)
        bgfx::updateTexture2D(handle, 0, 0, 0, 0, desc.width, desc.height,
                              bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
    return static_cast<render::TextureId>(handle.idx) + 1;
}

bool Context::UpdateTexture(render::TextureId id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                            std::span<const std::byte> pixels)
{
    if (!initialised_ || id == 0 || pixels.empty())
        return false;
    const bgfx::TextureHandle handle{static_cast<uint16_t>(id - 1)};
    bgfx::updateTexture2D(handle, 0, 0, x, y, width, height,
                          bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
    return true;
}

void Context::DestroyTexture(render::TextureId id)
{
    if (initialised_ && id != 0)
        bgfx::destroy(bgfx::TextureHandle{static_cast<uint16_t>(id - 1)});
}

uint16_t Context::BgfxTexture(render::TextureId id) const
{
    return id == 0 ? UINT16_MAX : static_cast<uint16_t>(id - 1);
}

namespace
{
std::string picture_key(std::string_view path)
{
    std::string key(path);
    for (char &c : key)
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}
} // namespace

void Context::set_picture_provider(render::GamePictureProvider *provider)
{
    picture_provider_ = provider;
    if (!provider)
        pictures_.clear();
}

render::GamePicture Context::AcquireGamePicture(std::string_view path)
{
    const std::string key = picture_key(path);
    auto it = pictures_.find(key);
    if (it == pictures_.end())
    {
        Picture entry;
        render::GamePictureProvider::Loaded loaded;
        if (!initialised_ || !picture_provider_ || !picture_provider_->Load(path, loaded))
            return render::GamePicture{};
        entry.token = loaded.token;
        entry.picture.state = render::GamePicture::State::Ready;
        entry.picture.texture = static_cast<render::TextureId>(loaded.bgfx_texture) + 1;
        entry.picture.width = loaded.width;
        entry.picture.height = loaded.height;
        it = pictures_.emplace(key, entry).first;
    }
    ++it->second.refs;
    return it->second.picture;
}

render::GamePicture Context::QueryGamePicture(std::string_view path)
{
    const auto it = pictures_.find(picture_key(path));
    return it == pictures_.end() ? render::GamePicture{} : it->second.picture;
}

void Context::ReleaseGamePicture(std::string_view path)
{
    const auto it = pictures_.find(picture_key(path));
    if (it == pictures_.end() || --it->second.refs != 0)
        return;
    if (picture_provider_)
        picture_provider_->Unload(it->second.token);
    pictures_.erase(it);
}

// ---- embedded shaders ----
} // namespace storm::shim

#include "storm_shaders/includes.inc"

namespace storm::shim
{
namespace
{
struct EmbeddedShader
{
    const char *name;
    render::ShaderBlob blob;
};
#define STORM_SHADER(name) {#name, STORM_SHADER_BLOB(name)},
const EmbeddedShader embedded[] = {
#include "storm_shaders/table.inc"
};
#undef STORM_SHADER
} // namespace

uint32_t embedded_shader_count()
{
    return static_cast<uint32_t>(std::size(embedded));
}

uint32_t shader_id_by_name(const char *name)
{
    for (uint32_t i = 0; i < std::size(embedded); ++i)
        if (strcmp(embedded[i].name, name) == 0)
            return i;
    logf("missing embedded shader %s", name);
    return UINT32_MAX;
}

uint32_t opaque_twin(uint32_t fs_id)
{
    static const std::vector<uint32_t> twins = [] {
        std::vector<uint32_t> result(std::size(embedded), UINT32_MAX);
        for (uint32_t i = 0; i < std::size(embedded); ++i)
        {
            const std::string twin = std::string(embedded[i].name) + "_opaque";
            for (uint32_t j = 0; j < std::size(embedded); ++j)
                if (twin == embedded[j].name)
                    result[i] = j;
        }
        return result;
    }();
    return fs_id < twins.size() ? twins[fs_id] : UINT32_MAX;
}

bgfx::ShaderHandle create_embedded_shader(uint32_t id)
{
    if (id >= std::size(embedded))
        return BGFX_INVALID_HANDLE;
    return render::CreateShader(embedded[id].blob, embedded[id].name);
}

} // namespace storm::shim

namespace storm::render
{
void SetLogSink(LogSink sink)
{
    storm::shim::log_sink.store(sink);
}
} // namespace storm::render
