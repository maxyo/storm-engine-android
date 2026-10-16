#include "mobile_image_cache.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace storm::mobile
{
std::string TexturePath(std::string path)
{
    if (path.empty() || path.size() > 1024)
        return {};
    for (auto &c : path)
    {
        if (c == '\\')
            c = '/';
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (path.front() == '/' || path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
        return {};
    size_t start = 0;
    while (start < path.size())
    {
        const auto end = path.find('/', start);
        auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            return {};
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (path.back() == '/')
        return {};
    if (path.rfind("resource/textures/", 0) != 0)
        path = "resource/textures/" + path;
    return path;
}
void ImageCache::screen(uint64_t revision)
{
    if (screen_ == revision)
        return;
    screen_ = revision;
    handles_.clear();
    descriptors_.clear();
    lru_.clear();
    bytes_ = 0;
    // Handles are never reused across screen lifetimes.
}
ImageRef ImageCache::reference(Descriptor d, uint32_t tint)
{
    d.path = TexturePath(std::move(d.path));
    if (d.path.empty())
        return {};
    for (float v : {d.uv.left, d.uv.top, d.uv.right, d.uv.bottom})
        if (!std::isfinite(v) || v < 0 || v > 1)
            return {};
    if (d.uv.left == d.uv.right || d.uv.top == d.uv.bottom)
        return {};
    std::ostringstream key;
    key << d.path << ':' << std::hexfloat << d.uv.left << ',' << d.uv.top << ',' << d.uv.right << ',' << d.uv.bottom;
    auto it = handles_.find(key.str());
    uint64_t handle;
    if (it != handles_.end())
        handle = it->second;
    else
    {
        if (descriptors_.size() >= 4096 || next_ > uint64_t(INT64_MAX))
            return {};
        handle = next_++;
        handles_[key.str()] = handle;
        descriptors_.emplace(handle, std::move(d));
    }
    const auto &stored = descriptors_.at(handle);
    return {handle, handle, stored.width, stored.height, tint};
}
const Descriptor *ImageCache::descriptor(uint64_t handle, uint64_t revision) const
{
    if (handle != revision)
        return nullptr;
    const auto it = descriptors_.find(handle);
    return it == descriptors_.end() ? nullptr : &it->second;
}
std::shared_ptr<const Pixels> ImageCache::get(const std::string &key)
{
    for (auto it = lru_.begin(); it != lru_.end(); ++it)
        if (it->key == key)
        {
            auto result = it->pixels;
            lru_.splice(lru_.begin(), lru_, it);
            return result;
        }
    return {};
}
void ImageCache::put(std::string key, std::shared_ptr<const Pixels> pixels)
{
    if (!pixels || pixels->rgba.size() > budget_)
        return;
    for (auto it = lru_.begin(); it != lru_.end(); ++it)
        if (it->key == key)
        {
            bytes_ -= it->pixels->rgba.size();
            lru_.erase(it);
            break;
        }
    while (!lru_.empty() && bytes_ + pixels->rgba.size() > budget_)
    {
        bytes_ -= lru_.back().pixels->rgba.size();
        lru_.pop_back();
    }
    bytes_ += pixels->rgba.size();
    lru_.push_front({std::move(key), std::move(pixels)});
}
void ImageCache::putPreview(const std::string &sourceKey, std::shared_ptr<const Pixels> source,
                            const std::string &outputKey, std::shared_ptr<const Pixels> output)
{
    if (source)
        put(sourceKey, source);
    // Prefer the reusable source over a crop that would immediately evict it.
    if (output && (!source || source->rgba.size() + output->rgba.size() <= budget_))
        put(outputKey, output);
}
} // namespace storm::mobile
