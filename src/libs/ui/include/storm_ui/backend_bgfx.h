// bgfx render backend: draws the UI inside a bgfx frame that someone else owns (the
// engine's frame owner, or a capture tool), in the frame owner's UI view range, after the
// game picture and at native surface resolution. Needs the build option STORM_UI_BGFX.
//
// It is also the frame client: register it with storm::render::FrameOwner::AddClient and
// attach the host; it then lays out on OnSurfaceChanged, draws in OnUiRender and drops and
// re-creates its GPU objects on device loss.
//
// RmlUi layers (box shadows, filters, masks) render into frame buffers, one bgfx view per
// render-target switch; passes of different elements share views. Work that only produces
// a texture (box-shadow images) may span frames when views are short. Effects that do not
// fit into the view range at all are not drawn and are logged as errors.
#pragma once

#include "storm_ui/render_backend.h"

#include <storm/render/frame_owner.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace storm::ui
{

class Host;

struct BgfxBackendOptions
{
    // Supplies game pictures (.tx) as GPU textures. Without it (a capture tool) pictures are
    // unavailable: placeholders stay and one line is logged. The engine host always sets it.
    storm::render::FrameOwner *owner = nullptr;
    // Multisampling of the layer frame buffers (0, 2 or 4). 2 matches the GL backend.
    int msaa_samples = 2;
};

class BgfxBackend : public RenderBackend, public storm::render::FrameClient
{
  public:
    // bgfx must be initialised (FrameOwner::DeviceReady()); main thread only.
    static std::unique_ptr<BgfxBackend> Create(const BgfxBackendOptions &options, std::string *error = nullptr);

    // The host this backend draws: OnUiRender calls Host::Frame(), OnSurfaceChanged calls
    // Host::Resize(). Null detaches.
    virtual void AttachHost(Host *host) = 0;
    // Pixels per dp used on surface changes instead of SurfaceMetrics::density (0: use it).
    virtual void SetDensity(float density) = 0;
    // Render target of the final UI image: a bgfx::FrameBufferHandle index, or 0xffff for the
    // surface back buffer (default). With a frame buffer, BeginFrame(true) clears it first.
    virtual void SetTarget(uint16_t frame_buffer) = 0;
    // bgfx view ids the backend may use. OnUiRender takes them from the FrameContext.
    virtual void SetViews(uint16_t first, uint16_t count) = 0;
    // Replaces a region of a texture made by GenerateTexture (rows top first, RGBA8).
    virtual bool UpdateTexture(Rml::TextureHandle texture, Rml::Rectanglei region, Rml::Span<const Rml::byte> rgba) = 0;
    // Queued texture-producing layer work not yet rendered (see the header comment).
    virtual size_t PendingWork() const = 0;
};

} // namespace storm::ui
