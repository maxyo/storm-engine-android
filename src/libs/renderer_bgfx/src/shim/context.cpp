#include "device.hpp"
#include "perf.hpp"
#include "storm/render/d3d_interop.hpp"
#include "storm/render/effekseer_device.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

namespace storm::shim
{

namespace
{
std::mutex trap_mutex;
std::map<std::string, uint64_t> trap_counts;

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
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "storm-bgfx", "%s", buffer);
#endif
    fprintf(stderr, "[storm-bgfx] %s\n", buffer);
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

uint16_t BgfxTextureOf(IDirect3DBaseTexture9 *texture, uint16_t *width, uint16_t *height)
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
    bgfx::Init init;
    if (window)
    {
        init.type = bgfx::RendererType::OpenGLES;
        if (!native_handles(window, &init.swapChain.nwh, &init.swapChain.ndt))
            return false;
    }
    else
    {
        // init_headless(): the shim does all its work, bgfx's Noop backend draws nothing.
        init.type = bgfx::RendererType::Noop;
        logf("headless: bgfx Noop renderer, nothing is drawn");
    }
    init.swapChain.width = width;
    init.swapChain.height = height;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8; // the UI views use stencil clip masks
    init.reset = reset_flags();
    init.fallback = false; // never continue on the Noop renderer
    if (!bgfx::init(init))
    {
        logf("bgfx::init failed");
        return false;
    }
    caps_ = bgfx::getCaps();
    flip_y_ = caps_->originBottomLeft;
    render::effekseer::AttachDevice({caps_->originBottomLeft, caps_->homogeneousDepth});
    initialised_ = true;
    surface_.width_px = width;
    surface_.height_px = height;
    surface_.valid = true;
    surface_in_use_ = true;
    if (window_)
        read_density();
    ++surface_.generation;
    logf("bgfx renderer=%s %ux%u originBottomLeft=%d homogeneousDepth=%d maxTextureSize=%u BC1=%d BC3=%d ETC2=%d BGRA8=%d",
         bgfx::getRendererName(bgfx::getRendererType()), width, height, caps_->originBottomLeft,
         caps_->homogeneousDepth, caps_->limits.maxTextureSize,
         (caps_->formats[bgfx::TextureFormat::BC1] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
         (caps_->formats[bgfx::TextureFormat::BC3] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
         (caps_->formats[bgfx::TextureFormat::ETC2] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
         (caps_->formats[bgfx::TextureFormat::BGRA8] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0);
    create_resources();
    for (auto &entry : clients_)
    {
        entry.client->OnDeviceRestored();
        entry.client->OnSurfaceChanged(surface_);
    }
    return true;
}

void Context::create_resources()
{
    static const char *names[kSamplerUnits] = {"s_tex0", "s_tex1", "s_tex2", "s_tex3", "s_tex4", "s_cube0",
                                                "s_cube1", "s_cube2", "s_vol0", "s_vol1", "s_vol2",
                                                // pixel-port extras: cube at stage 3, volume at stage 4 (sea)
                                                "s_cube3", "s_vol4"};
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
}

void Context::shutdown()
{
    if (!initialised_)
        return;
    for (auto &entry : clients_)
        entry.client->OnDeviceLost();
    dump_traps();
    render::effekseer::DetachDevice();
    bgfx::shutdown();
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
    chain.formatDepthStencil = bgfx::TextureFormat::D24S8; // the UI views use stencil clip masks
    bgfx::reset(reset_flags(), &chain);
    // A display change (fold, split screen, wm size/density) usually changes the density as well.
    read_density();
    logf("surface resized to %ux%u, density %.2f", width, height, surface_.density);
    for (auto &entry : clients_)
        entry.client->OnSurfaceChanged(surface_);
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
    restore_requested_ = true;
}

uint32_t Context::reset_flags() const
{
    // Flip right after rendering: no swap of a previous frame is ever pending when the surface changes.
    return (vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE) | BGFX_RESET_FLIP_AFTER_RENDER |
           (surface_epoch_flag_ ? BGFX_RESET_FLUSH_AFTER_RENDER : BGFX_RESET_NONE);
}

void Context::surface_lost()
{
    if (surface_.valid)
    {
        surface_.valid = false;
        ++surface_.generation;
        frame_aborted_ = frame_open_;
        logf("surface lost: presentation suspended");
    }
    {
        std::lock_guard lock(release_mutex_);
        surface_in_use_ = false;
        release_requested_ = false;
    }
    release_done_.notify_all();
}

void Context::request_surface_release()
{
    std::unique_lock lock(release_mutex_);
    if (!surface_in_use_)
        return;
    release_requested_ = true;
    // Only bgfx::frame() touches the EGL surface. One frame can take a second on slow GPUs (the
    // emulator), so the wait covers a whole frame; outside a frame this returns at once.
    if (!release_done_.wait_for(lock, std::chrono::seconds(3), [this] { return !in_frame_; }))
        logf("ERROR: a bgfx frame still runs 3 s after Android asked for the surface; it is destroyed while in use");
}

bool Context::run_frame(uint32_t flags, uint32_t *frame_number)
{
    {
        std::lock_guard lock(release_mutex_);
        // Never on a surface that is gone or going (background, surfaceDestroyed, read-backs in between).
        in_frame_ = surface_in_use_ && !release_requested_;
    }
    if (!in_frame_)
    {
        surface_lost();
        return false;
    }
    const uint32_t number = bgfx::frame(flags);
    ++submitted_frames_;
    {
        std::lock_guard lock(release_mutex_);
        in_frame_ = false;
    }
    release_done_.notify_all();
    if (frame_number)
        *frame_number = number;
    return true;
}

void Context::poll_surface()
{
    if (release_requested_.load(std::memory_order_relaxed))
        surface_lost();
    if (restore_requested_.exchange(false) && initialised_ && !surface_.valid)
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
    restore_pending_ = true;
    try_restore();
}

void Context::try_restore()
{
    bgfx::SwapChain chain;
    // SDL drops its native window when Android destroys the surface and holds a new one only after
    // surfaceCreated; until then there is nothing to attach to and the restore stays pending.
    if (!native_handles(window_, &chain.nwh, &chain.ndt))
    {
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
    chain.formatDepthStencil = bgfx::TextureFormat::D24S8; // the UI views use stencil clip masks
    // A new ANativeWindow can reuse the old address; alternating this otherwise harmless flag makes
    // bgfx re-create the EGL surface on every restore.
    surface_epoch_flag_ = !surface_epoch_flag_;
    {
        // From here on a new surfaceDestroyed waits for our frames.
        std::lock_guard lock(release_mutex_);
        surface_in_use_ = true;
    }
    bgfx::reset(reset_flags(), &chain);
    if (frame_aborted_)
    {
        // Draws submitted before the surface went away belong to a frame that was never finished.
        if (!run_frame(BGFX_FRAME_DISCARD))
            return; // the new surface is already going away again
        frame_aborted_ = false;
    }
    surface_.valid = true;
    ++surface_.generation;
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

void Context::pace()
{
    if (frame_limit_ == 0)
        return;
    const double interval = 1.0 / frame_limit_;
    const double target = last_present_ + interval;
    double now = now_seconds();
    if (now < target)
    {
        std::this_thread::sleep_for(std::chrono::duration<double>(target - now));
        now = now_seconds();
    }
    // Never accumulate debt after a long frame.
    last_present_ = now > target + interval ? now : target;
}

void Context::present(bgfx::TextureHandle game_color, uint32_t game_width, uint32_t game_height)
{
    if (!initialised_)
        return;
    begin_frame_if_needed();
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
        for (auto &entry : clients_)
            entry.client->OnUiRender(frame_);
        pace();
        const auto started = std::chrono::steady_clock::now();
        if (run_frame(BGFX_FRAME_NONE))
            perf_frame_presented(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
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
    const uint8_t *data;
    uint32_t size;
};
#define STORM_SHADER(name) {#name, name, sizeof(name)},
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

bgfx::ShaderHandle create_embedded_shader(uint32_t id)
{
    if (id >= std::size(embedded))
        return BGFX_INVALID_HANDLE;
    const bgfx::ShaderHandle handle = bgfx::createShader(bgfx::makeRef(embedded[id].data, embedded[id].size));
    bgfx::setName(handle, embedded[id].name);
    return handle;
}

} // namespace storm::shim
