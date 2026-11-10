// Frame owner: the single place that owns the bgfx context, the surface and
// the order of views inside a frame. Stable contract for other libraries
// (first consumer: src/libs/ui). API version history is at the bottom.
//
// Frame layout (bgfx view ids, all ViewMode::Sequential):
//   [0 .. kGameViewEnd)         game: D3D9-shaped shim. Renders into an offscreen
//                               "game backbuffer" (may be smaller than the surface:
//                               dynamic resolution) and other render targets.
//   kCompositeView              game backbuffer -> surface backbuffer (scaled blit).
//   [kUiViewBegin .. +kUiViewCount)  client views, drawn LAST, at native surface
//                               resolution, never scaled by dynamic resolution.
//
// Threading: bgfx runs single-threaded (BGFX_CONFIG_MULTITHREADED=0). Every
// callback below and every bgfx call must happen on the engine main thread,
// which is also the thread that owns the EGL context.
//
// Clients may call bgfx directly (link the `storm_bgfx` CMake target; never
// pin bgfx yourself) but must restrict themselves to their view range and
// must not call bgfx::init/shutdown/reset/frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace storm::render
{

inline constexpr uint32_t kFrameOwnerApiVersion = 1;

// bgfx::ViewId values. Compile-time constants so clients can size tables.
inline constexpr uint16_t kGameViewEnd = 224;
inline constexpr uint16_t kCompositeView = 224;
inline constexpr uint16_t kUiViewBegin = 232;
inline constexpr uint16_t kUiViewCount = 16;

struct SurfaceMetrics
{
    uint32_t width_px = 0;  // native surface size (landscape)
    uint32_t height_px = 0;
    float density = 1.0f;   // px per dp (Android DisplayMetrics.density; 1.0 on the host unless overridden)
    uint32_t generation = 0; // increments on every size/density change and surface re-creation
    bool valid = false;      // false while the app is in background / the surface is lost
};

struct FrameContext
{
    uint64_t frame_index = 0;
    double delta_seconds = 0.0; // wall time since the previous presented frame
    SurfaceMetrics surface;
    uint16_t ui_view_begin = kUiViewBegin;
    uint16_t ui_view_count = kUiViewCount;
};

// Implemented by clients. All methods have empty defaults.
class FrameClient
{
  public:
    virtual ~FrameClient() = default;

    // Start of an engine frame, before the game submits anything.
    virtual void OnFrameBegin(const FrameContext &) {}

    // After the scene, the legacy interface and the composite blit were
    // submitted, right before bgfx::frame(). Submit to the UI views here.
    // The UI views are pre-configured each frame: frame buffer = surface
    // backbuffer, rect = full surface, no clear, Sequential. A client may
    // re-target its own views (bgfx::setViewFrameBuffer etc.) for layers.
    // Called every presented frame while the surface is valid.
    virtual void OnUiRender(const FrameContext &) {}

    // After bgfx::frame() returned.
    virtual void OnFrameEnd(const FrameContext &) {}

    // Surface size or density changed (also sent once on registration).
    virtual void OnSurfaceChanged(const SurfaceMetrics &) {}

    // GPU objects are gone (context lost / bgfx shut down): drop every bgfx
    // handle without calling bgfx::destroy. Followed by OnDeviceRestored().
    virtual void OnDeviceLost() {}
    // bgfx is initialised again: re-create GPU objects.
    virtual void OnDeviceRestored() {}

    // Return true while the client animates and needs frames presented at
    // display rate even if the game is paused or produces no new image.
    virtual bool WantsContinuousFrames() { return false; }
};

// Opaque texture id managed by the frame owner. 0 is "no texture".
using TextureId = uintptr_t;

enum class PixelFormat : uint8_t
{
    RGBA8,      // 4 bytes per pixel, R first; alpha mode is the caller's business
    A8,         // 1 byte per pixel
    ETC2_RGB8,  // compressed payloads: full mip chain, tightly packed, top mip first
    ETC2_RGBA8,
    ETC2_RGB8A1,
};

enum class TextureFlags : uint32_t
{
    None = 0,
    Repeat = 1u << 0,    // default is clamp-to-edge on both axes
    Point = 1u << 1,     // default is linear min/mag (+ linear mip if mips present)
    Updatable = 1u << 2, // UpdateTexture() will be used (RGBA8/A8 only)
};
constexpr TextureFlags operator|(TextureFlags a, TextureFlags b)
{
    return static_cast<TextureFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool operator&(TextureFlags a, TextureFlags b)
{
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

struct TextureDesc
{
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t mip_count = 1;
    PixelFormat format = PixelFormat::RGBA8;
    TextureFlags flags = TextureFlags::None;
};

// Result of AcquireGamePicture().
struct GamePicture
{
    enum class State : uint8_t
    {
        Pending, // still decoding/transcoding: poll again next frame
        Ready,
        Failed, // file missing or unsupported
    };
    State state = State::Failed;
    TextureId texture = 0; // valid when Ready
    uint16_t width = 0;    // size of the top mip actually on the GPU
    uint16_t height = 0;
    // Game .tx pictures use STRAIGHT (non-premultiplied) alpha.
    bool premultiplied_alpha = false;
};

class FrameOwner
{
  public:
    // nullptr until the renderer service created the device; stays valid
    // until engine shutdown (the object survives device loss).
    static FrameOwner *Get();

    virtual uint32_t ApiVersion() const = 0;

    // Clients are called in ascending `order`, ties in registration order.
    virtual void AddClient(FrameClient *client, int order = 0) = 0;
    virtual void RemoveClient(FrameClient *client) = 0;

    virtual SurfaceMetrics Surface() const = 0;
    virtual bool DeviceReady() const = 0; // bgfx initialised and surface valid

    // ---- textures (helpers over bgfx; clients may also use bgfx directly) ----
    // `pixels` may be empty for Updatable textures. Rows are top first.
    // Returns 0 on failure (bad size, unsupported format, device not ready).
    virtual TextureId CreateTexture(const TextureDesc &desc, std::span<const std::byte> pixels) = 0;
    // Tightly packed rows in the texture's format; mip 0 only.
    virtual bool UpdateTexture(TextureId id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                               std::span<const std::byte> pixels) = 0;
    virtual void DestroyTexture(TextureId id) = 0;
    // bgfx::TextureHandle::idx for bgfx::setTexture, or 0xffff.
    virtual uint16_t BgfxTexture(TextureId id) const = 0;
    virtual bool IsFormatSupported(PixelFormat format) const = 0;

    // ---- game pictures (.tx from the player's files) ----
    // `path` is relative to RESOURCE/Textures, without extension, '/' or '\\'
    // separators, case-insensitive. Non-blocking and reference counted: call
    // again each frame while Pending (no extra reference is taken for a
    // repeated Pending/Ready query of the same id; see ReleaseGamePicture).
    // Each successful first Acquire for a path adds one reference.
    virtual GamePicture AcquireGamePicture(std::string_view path) = 0;
    // Non-owning re-query by path after Acquire returned Pending.
    virtual GamePicture QueryGamePicture(std::string_view path) = 0;
    virtual void ReleaseGamePicture(std::string_view path) = 0;

    // ---- pacing ----
    // Wall-clock frame limiter in frames per second, 0 = display rate.
    virtual void SetFrameRateLimit(uint32_t fps) = 0;

  protected:
    virtual ~FrameOwner() = default;
};

// Platform surface lifecycle, reported by the application loop (Android pause/resume).
// Between Lost and Restored nothing is presented and FrameClient::OnUiRender is not called.
void NotifySurfaceLost();
void NotifySurfaceRestored();

} // namespace storm::render

// Version history
//   1  initial: view range, hooks, surface metrics, texture helpers, game pictures.
