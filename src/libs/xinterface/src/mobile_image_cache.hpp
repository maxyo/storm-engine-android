#pragma once
#include "mobile_image_decode.hpp"
#include <list>
#include <map>
#include <memory>

namespace storm::mobile
{
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
    // Optional public source identity. Never contains filesystem paths or cache handles.
    struct Source
    {
        std::string kind, group, image, mode, atlas;
        int pic = -1;
    } source;
};
// Game-thread-only registry and LRU. Workers receive copies/immutable shared pixels.
class ImageCache
{
  public:
    explicit ImageCache(size_t budget = 16 * 1024 * 1024) : budget_(budget)
    {
    }
    void screen(uint64_t revision);
    uint64_t screenRevision() const
    {
        return screen_;
    }
    ImageRef reference(Descriptor descriptor, uint32_t tint = 0xffffffff);
    const Descriptor *descriptor(uint64_t handle, uint64_t revision) const;
    std::shared_ptr<const Pixels> get(const std::string &key);
    void put(std::string key, std::shared_ptr<const Pixels> pixels);
    void putPreview(const std::string &sourceKey, std::shared_ptr<const Pixels> source, const std::string &outputKey,
                    std::shared_ptr<const Pixels> output);
    size_t bytes() const
    {
        return bytes_;
    }
    size_t descriptors() const
    {
        return descriptors_.size();
    }
    static std::string sourceKey(const Descriptor &d)
    {
        return "source:" + d.path;
    }
    static std::string outputKey(uint64_t handle, uint32_t edge)
    {
        return "crop:" + std::to_string(handle) + ":" + std::to_string(edge);
    }

  private:
    struct Entry
    {
        std::string key;
        std::shared_ptr<const Pixels> pixels;
    };
    uint64_t screen_ = 0, next_ = 1;
    size_t budget_, bytes_ = 0;
    std::map<std::string, uint64_t> handles_;
    std::map<uint64_t, Descriptor> descriptors_;
    std::list<Entry> lru_;
};
// Only paths under the game's texture namespace; no absolute/traversal Java paths.
std::string TexturePath(std::string path);
} // namespace storm::mobile
