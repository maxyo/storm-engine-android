#pragma once

#include <storm/render/frame_owner.hpp>

class Renderer;

namespace storm
{
// Serves FrameOwner::AcquireGamePicture from the renderer's .tx texture cache.
class GamePictures final : public render::GamePictureProvider
{
  public:
    explicit GamePictures(Renderer &renderer) : renderer_(renderer) {}
    bool Load(std::string_view path, Loaded &out) override;
    void Unload(uint32_t token) override;

  private:
    Renderer &renderer_;
};
} // namespace storm
