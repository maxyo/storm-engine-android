// The only renderer-specific seam of the UI runtime. Everything above it (documents,
// components, icons, game pictures) talks to Rml::RenderInterface and never to a
// graphics API, so the UI moves with the engine renderer:
//
//   * GL3 backend (this library): a GLES 3 / GL 3.3 context owned by the platform host.
//     Used by the host capture tool and as the Android bridge until bgfx is live.
//   * bgfx backend (target): implemented next to the bgfx renderer; draws inside the
//     engine frame in the last view. See src/libs/ui/README.md.
#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <memory>
#include <string>
#include <vector>

namespace storm::ui
{

class RenderBackend
{
  public:
    virtual ~RenderBackend() = default;
    virtual Rml::RenderInterface &Interface() = 0;
    virtual void SetViewport(int width, int height) = 0;
    // `clear` wipes the target to transparent first (stand-alone UI surface). A backend
    // that draws over the game frame ignores it.
    virtual void BeginFrame(bool clear) = 0;
    virtual void EndFrame() = 0;
};

// Needs a current GLES 3.2 (Android) or GL 3.3 core (desktop) context.
std::unique_ptr<RenderBackend> CreateGl3Backend(std::string *error = nullptr);

// For backend authors: decodes a PNG/JPEG of the UI pack (through Rml::GetFileInterface)
// to premultiplied RGBA8. Game pictures do not come this way; see game_image.cpp.
bool DecodePackImage(const std::string &source, int &width, int &height, std::vector<unsigned char> &rgba);

} // namespace storm::ui
