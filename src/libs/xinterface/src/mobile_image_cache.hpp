#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace storm::mobile
{
struct Crop
{
    float left = 0, top = 0, right = 1, bottom = 1;
};
struct Descriptor
{
    std::string path;
    Crop uv;
    uint32_t width = 0, height = 0;
};
struct ImageRef
{
    uint64_t handle = 0, imageRevision = 0;
    uint32_t width = 0, height = 0, tintArgb = 0xffffffff;
    // Optional public source identity. Never contains filesystem paths or cache handles; a
    // "texture" source names a file of the game's texture namespace ("interfaces/maps/kayman")
    // and the rectangle of it the picture shows (uv: left, top, right, bottom).
    struct Source
    {
        std::string kind, group, image, mode, atlas;
        int pic = -1;
        float uv[4] = {0.f, 0.f, 1.f, 1.f};
    } source;
};
// Game-thread-only registry: one opaque handle per picture (texture path and rectangle) of a
// screen lifetime. The interface model names pictures by their source; the portable UI draws
// them through the renderer.
class ImageCache
{
  public:
    void screen(uint64_t revision);
    uint64_t screenRevision() const
    {
        return screen_;
    }
    ImageRef reference(Descriptor descriptor, uint32_t tint = 0xffffffff);
    size_t descriptors() const
    {
        return descriptors_.size();
    }

  private:
    uint64_t screen_ = 0, next_ = 1;
    std::map<std::string, uint64_t> handles_;
    std::map<uint64_t, Descriptor> descriptors_;
};
// Only paths under the game's texture namespace; no absolute/traversal Java paths.
std::string TexturePath(std::string path);
} // namespace storm::mobile
