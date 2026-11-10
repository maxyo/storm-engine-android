#include "device.hpp"
#include "storm/render/d3d_interop.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

#ifdef __ANDROID__
#include <android/log.h>
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
        logf("TRAP: unported D3D9 entry point %s", method);
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

void NotifySurfaceLost()
{
    shim::Context::instance().surface_lost();
}

void NotifySurfaceRestored()
{
    shim::Context::instance().surface_restored();
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
    if (initialised_)
    {
        resize(width, height);
        return true;
    }
    window_ = window;
    vsync_ = vsync;
    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGLES;
    if (!native_handles(window, &init.swapChain.nwh, &init.swapChain.ndt))
        return false;
    init.swapChain.width = width;
    init.swapChain.height = height;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8; // the UI views use stencil clip masks
    init.reset = vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    if (!bgfx::init(init))
    {
        logf("bgfx::init failed");
        return false;
    }
    caps_ = bgfx::getCaps();
    flip_y_ = caps_->originBottomLeft;
    initialised_ = true;
    surface_.width_px = width;
    surface_.height_px = height;
    surface_.valid = true;
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
}

void Context::shutdown()
{
    if (!initialised_)
        return;
    for (auto &entry : clients_)
        entry.client->OnDeviceLost();
    dump_traps();
    bgfx::shutdown();
    initialised_ = false;
    surface_.valid = false;
    pictures_.clear();
    picture_provider_ = nullptr;
}

void Context::resize(uint32_t width, uint32_t height)
{
    if (!initialised_ || (width == surface_.width_px && height == surface_.height_px))
        return;
    surface_.width_px = width;
    surface_.height_px = height;
    ++surface_.generation;
    bgfx::SwapChain chain;
    native_handles(window_, &chain.nwh, &chain.ndt);
    chain.width = width;
    chain.height = height;
    chain.formatDepthStencil = bgfx::TextureFormat::D24S8; // the UI views use stencil clip masks
    bgfx::reset(vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &chain);
    for (auto &entry : clients_)
        entry.client->OnSurfaceChanged(surface_);
}

void Context::set_density(float density)
{
    if (density <= 0.0f || density == surface_.density)
        return;
    surface_.density = density;
    ++surface_.generation;
    for (auto &entry : clients_)
        entry.client->OnSurfaceChanged(surface_);
}

void Context::surface_lost()
{
    if (!surface_.valid)
        return;
    surface_.valid = false;
    ++surface_.generation;
    logf("surface lost: presentation suspended");
}

void Context::surface_restored()
{
    if (!initialised_ || surface_.valid)
        return;
    bgfx::SwapChain chain;
    if (!native_handles(window_, &chain.nwh, &chain.ndt))
        return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (w > 0 && h > 0)
    {
        surface_.width_px = static_cast<uint32_t>(w);
        surface_.height_px = static_cast<uint32_t>(h);
    }
    chain.width = surface_.width_px;
    chain.height = surface_.height_px;
    chain.formatDepthStencil = bgfx::TextureFormat::D24S8; // the UI views use stencil clip masks
    bgfx::reset(vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &chain);
    surface_.valid = true;
    ++surface_.generation;
    logf("surface restored %ux%u", surface_.width_px, surface_.height_px);
    for (auto &entry : clients_)
        entry.client->OnSurfaceChanged(surface_);
}

void Context::begin_frame_if_needed()
{
    if (frame_open_ || !initialised_)
        return;
    frame_open_ = true;
    frame_.surface = surface_;
    for (auto &entry : clients_)
        entry.client->OnFrameBegin(frame_);
}

void Context::draw_fullscreen(uint16_t view, bgfx::TextureHandle texture, uint32_t sampler_flags, uint64_t state,
                              float u0, float v0, float u1, float v1)
{
    if (bgfx::getAvailTransientVertexBuffer(3, blit_layout_) < 3)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 3, blit_layout_);
    // One oversized triangle; (u0,v0) is the corner at clip (-1,+1).
    const float du = u1 - u0, dv = v1 - v0;
    const float vertices[12] = {-1.0f, 1.0f, u0, v0, 3.0f, 1.0f, u0 + 2.0f * du, v0, -1.0f, -3.0f, u0, v0 + 2.0f * dv};
    memcpy(tvb.data, vertices, sizeof(vertices));
    bgfx::setVertexBuffer(0, &tvb);
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

void Context::present(bgfx::TextureHandle game_color, uint32_t, uint32_t)
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
        if (bgfx::isValid(game_color))
            draw_fullscreen(render::kCompositeView, game_color, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
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
        bgfx::frame();
    }
    else
    {
        // Background: drop everything submitted this frame without touching the surface.
        bgfx::frame(BGFX_FRAME_DISCARD);
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

void Context::flush()
{
    if (initialised_)
        bgfx::frame(BGFX_FRAME_FLUSH);
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
