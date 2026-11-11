// The only renderer-specific seam of the UI runtime. Everything above it (documents,
// components, icons, game pictures) talks to Rml::RenderInterface and never to a
// graphics API, so the UI moves with the engine renderer:
//
//   * GL backend (backend_gl3.h, target storm_ui_gl3): a GLES 3 / GL 3.3 context owned by a
//     platform host without the engine (application screens, host captures).
//   * bgfx backend (backend_bgfx.h, target storm_ui_bgfx): draws inside the engine frame in
//     the frame owner's UI view range (the game). See src/libs/ui/README.md.
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

    // Game pictures (.tx). A backend that draws inside the engine frame gets them from the
    // renderer: Pending keeps the element's placeholder and polls again next frame, Ready
    // means GamePictureSource(texture) loads as an RmlUi texture, Failed keeps the
    // placeholder for good (logged by the backend). Unavailable: the backend has no picture
    // source at all (a capture tool without a frame owner), the neutral placeholder stays,
    // as for a GL host without a game root. A backend without a renderer behind it
    // (GL) answers DecodeOnCpu: the UI reads the file itself (Config::game_picture_loader
    // or the built-in reader). That choice belongs to the backend, it is never a fallback.
    enum class PictureState
    {
        Pending,
        Ready,
        Failed,
        Unavailable,
        DecodeOnCpu
    };
    virtual PictureState AcquireGamePicture(const std::string &texture)
    {
        (void)texture;
        return PictureState::DecodeOnCpu;
    }
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
