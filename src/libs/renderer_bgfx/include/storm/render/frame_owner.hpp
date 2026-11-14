// Frame owner: the single place that owns the bgfx context, the surface and
// the order of views inside a frame. Stable contract for other libraries
// (first consumer: src/libs/ui). API version history is at the bottom.
//
// Frame layout (bgfx view ids, all ViewMode::Sequential):
//   [kPreGameViewBegin .. +kPreGameViewCount)  reserved for passes that precede the scene. Its last id is the
//                               renderer's parallax probe, moved right behind the scene's last view while it runs.
//   [kGameViewBegin .. kGameViewEnd)  game: D3D9-shaped shim. Renders into an offscreen
//                               "game backbuffer" (its size may differ from the surface's:
//                               scaled blit) and other render targets.
//   [kPostViewBegin .. +kPostViewCount)  reserved for post-processing of the game target.
//   kCompositeView              game backbuffer -> surface backbuffer (scaled blit).
//   [kUiViewBegin .. +kUiViewCount)  client views, drawn LAST, at native surface
//                               resolution, never scaled with the game backbuffer.
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
#include <filesystem>
#include <span>
#include <string_view>

namespace storm::render
{

inline constexpr uint32_t kFrameOwnerApiVersion = 6;

// bgfx::ViewId values. Compile-time constants so clients can size tables.
inline constexpr uint16_t kPreGameViewBegin = 0; // passes that must run before the scene (e.g. a shadow map)
inline constexpr uint16_t kPreGameViewCount = 8;
inline constexpr uint16_t kGameViewBegin = 8;
inline constexpr uint16_t kGameViewEnd = 224;
inline constexpr uint16_t kPostViewBegin = 224; // post-processing of the game target, before the composite
inline constexpr uint16_t kPostViewCount = 24;
inline constexpr uint16_t kCompositeView = 248;
inline constexpr uint16_t kUiViewBegin = 256;
inline constexpr uint16_t kUiViewCount = 64;

struct SurfaceMetrics
{
    uint32_t width_px = 0;  // native surface size (landscape)
    uint32_t height_px = 0;
    float density = 1.0f;   // px per dp: display DPI / 160 as reported by SDL (1.0 when unknown)
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
    TextureId texture = 0; // valid when Ready; owned by the frame owner: never pass it to DestroyTexture
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
    // The game's offscreen target of the current frame: bgfx texture ids of its colour (RGBA8) and
    // depth (D24S8, sampleable) attachments. Both are 0xffff before the first frame.
    struct GameTarget
    {
        uint16_t color = 0xffff, depth = 0xffff;
        uint32_t width = 0, height = 0;
    };
    virtual GameTarget CurrentGameTarget() const = 0;
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

  protected:
    virtual ~FrameOwner() = default;
};

// Implemented by the renderer service, which owns the .tx loader and its reference counts.
class GamePictureProvider
{
  public:
    struct Loaded
    {
        uint32_t token = 0;            // provider handle passed back to Unload()
        uint16_t bgfx_texture = 0xffff; // bgfx::TextureHandle::idx
        uint16_t width = 0, height = 0;
    };
    virtual ~GamePictureProvider() = default;
    // `path` as in FrameOwner::AcquireGamePicture. Returns false when the picture does not exist.
    virtual bool Load(std::string_view path, Loaded &out) = 0;
    virtual void Unload(uint32_t token) = 0;
};
// nullptr detaches (pictures acquired afterwards fail; live ones are forgotten, not unloaded).
void SetGamePictureProvider(GamePictureProvider *provider);

// bgfx's render thread: on by default (the GL backend, the driver and the present run off the game thread). Off
// for measurements (engine.ini render_thread = 0): bgfx then renders inside the game thread's bgfx::frame().
// Call before the device is created; the choice is logged when it is ("render thread: ...").
void SetRenderThread(bool enabled);
// With the render thread: whether the game thread simulates the next frame while the render thread still renders
// this one. -1 (default): only while game work + render work do not fit the frame (no added input latency where
// they fit); 0 never; 1 always (engine.ini render_overlap, measurements). Each change is logged.
void SetFrameOverlap(int mode);

// The bgfx backend of the game window (src/shim/backend_select.hpp). Auto: Vulkan on the devices it is chosen for
// (a Vulkan 1.1 device of a GPU family measured faster on it, whose Vulkan start and self-test pass), OpenGL ES on
// the others. OpenGLES / Vulkan ask for that backend (engine.ini render_backend = gles / vulkan, measurements);
// Vulkan asked for on a device that cannot run it still ends on OpenGL ES, with an error. Call before the device
// is created; the choice and its reason are logged when it is ("render backend: ...").
enum class Backend
{
    Auto,
    OpenGLES,
    Vulkan,
};
// `state_directory` (under the cache root; may be cleared at any time): where a Vulkan start leaves its marker
// until frames were presented, so a driver that kills the process in it is not started on Vulkan again.
void SetRenderBackend(Backend backend, const std::filesystem::path &state_directory);
// Vulkan only: whether a scene draw whose pipeline does not exist yet waits for it off the render thread (on, the
// default: the draw appears a few frames later and no frame waits for the driver's compile) or the render thread
// creates it at the draw (engine.ini vulkan_async_pipelines = 0, measurements). Any time.
void SetVulkanAsyncPipelines(bool enabled);
// Vulkan with async pipelines: one more than the bgfx frame number of the last frame in which a scene draw was left
// out while its pipeline was being created; 0 while none was, and always on OpenGL ES. It changes whenever another
// frame leaves a draw out: a frame shown after it stayed unchanged had every draw (tools/scene_sweep waits for that).
uint32_t LastIncompleteFrame();
// The backend the device runs and why, as logged; empty before the first device.
const char *RenderBackendChoice();

// Platform surface lifecycle, reported by the application loop (Android pause/resume).
// Between Lost and Restored nothing is presented and FrameClient::OnUiRender is not called.
void NotifySurfaceLost();
void NotifySurfaceRestored();

// Where the game picture is shown on the surface, as fractions of the surface size. The game target
// keeps its size when the window changes and is fitted with its own aspect ratio (letterbox), so
// touches given relative to the whole view must be mapped through this rectangle.
struct PictureRect
{
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
};
PictureRect GamePictureRect();

// A paused game under a menu of the UI layer (game thread, once per frame before the engine's frame): while
// HoldGamePicture(true) lasts, the next presented game picture is kept and presented again in place of the game's
// own frames. GamePictureHeld() is true from the frame after that picture was kept: the engine then draws no scene
// (its scene realize layers are held). A change of the renderer settings or of the game picture's size drops the
// kept picture: the next frame is drawn in full and kept again. HoldGamePicture(false) releases it at once.
void HoldGamePicture(bool hold);
bool GamePictureHeld();

} // namespace storm::render

// Version history
//   6  HoldGamePicture() / GamePictureHeld(): the last game picture presented again under a pause menu.
//   1  initial: view range, hooks, surface metrics, texture helpers, game pictures.
//   4  GamePictureRect(): the letterboxed game picture on the surface.
//   5  SetFrameRateLimit() removed: the renderer paces presents itself (Swappy on Android).
//   3  view ranges reserved before the scene and for post-processing (game views are 8..223,
//      composite 248); CurrentGameTarget().
//   2  UI views moved to 256..319 (64 views); density reported from the display; game picture
//      provider; surface lost/restored notifications.
