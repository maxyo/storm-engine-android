#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace storm::mobile
{
// Owned straight-alpha RGBA, top-left origin, tightly packed width*4 byte rows.
struct Pixels
{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
struct Crop
{
    float left = 0, top = 0, right = 1, bottom = 1;
};
struct DecodeResult
{
    Pixels pixels;
    std::string error;
    explicit operator bool() const
    {
        return error.empty();
    }
};
constexpr size_t MaxEncodedBytes = 16 * 1024 * 1024;
constexpr size_t MaxDecodedBytes = 16 * 1024 * 1024;
// No VFS, renderer, JNI or game-thread state is accessed by either function.
DecodeResult Decode(const std::vector<uint8_t> &bytes, const std::string &extension);
DecodeResult CropResize(const Pixels &source, Crop uv, uint32_t maxEdge);
} // namespace storm::mobile
