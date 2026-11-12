// The only renderer-specific seam of the UI runtime. Everything above it (documents,
// components, icons, game pictures) talks to Rml::RenderInterface and never to a
// graphics API, so the UI moves with the engine renderer:
//
// The backend is bgfx (backend_bgfx.h, target storm_ui_bgfx): inside the engine frame for the
// game, on a bgfx context of the host's own for the screens without the engine and for captures.
// See src/libs/ui/README.md.
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
    // True when the frame just ended was not shown: it would have been drawn without textures the
    // backend is still making (layer textures: box shadows), so the frame before it stays on the
    // screen. The host draws the frame again until it is shown, and tells nobody it was drawn.
    // A backend that makes every texture within the frame that draws it never holds one.
    virtual bool FrameHeld() const
    {
        return false;
    }

    // Game pictures (.tx). A backend that draws inside the engine frame gets them from the
    // renderer: Pending keeps the element's placeholder and polls again next frame, Ready
    // means GamePictureSource(texture) loads as an RmlUi texture, Failed keeps the
    // placeholder for good (logged by the backend). Unavailable: the backend has no picture
    // source at all (a screen without the engine, a capture without game files): the neutral
    // placeholder stays.
    enum class PictureState
    {
        Pending,
        Ready,
        Failed,
        Unavailable
    };
    virtual PictureState AcquireGamePicture(const std::string &texture) = 0;
};

// RmlUi texture source of a renderer-supplied game picture (see AcquireGamePicture).
inline std::string GamePictureSource(const std::string &texture)
{
    return "storm-game-picture:" + texture;
}

// For backend authors: decodes a PNG/JPEG of the UI pack (through Rml::GetFileInterface)
// to premultiplied RGBA8. Game pictures do not come this way; see game_image.cpp.
bool DecodePackImage(const std::string &source, int &width, int &height, std::vector<unsigned char> &rgba);

} // namespace storm::ui
