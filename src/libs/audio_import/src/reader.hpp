#pragma once
// Bounds-checked little-endian reads over a byte view.

#include "audio_import/audio_import.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

namespace storm::audio_import::detail
{
class Reader
{
  public:
    Reader(ByteView data, const char *what) : data_(data), what_(what)
    {
    }

    size_t position() const
    {
        return position_;
    }
    size_t size() const
    {
        return data_.size();
    }
    size_t remaining() const
    {
        return data_.size() - position_;
    }
    void seek(size_t position)
    {
        if (position > data_.size())
            fail("seek past end");
        position_ = position;
    }
    ByteView take(size_t count)
    {
        if (count > remaining())
            fail("truncated");
        ByteView view = data_.subspan(position_, count);
        position_ += count;
        return view;
    }
    uint8_t u8()
    {
        return take(1)[0];
    }
    uint16_t u16()
    {
        auto b = take(2);
        return static_cast<uint16_t>(b[0] | b[1] << 8);
    }
    uint32_t u24()
    {
        auto b = take(3);
        return b[0] | b[1] << 8 | static_cast<uint32_t>(b[2]) << 16;
    }
    uint32_t u32()
    {
        auto b = take(4);
        return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
    }
    uint64_t u64()
    {
        uint64_t low = u32();
        return low | static_cast<uint64_t>(u32()) << 32;
    }
    std::string fourcc()
    {
        auto b = take(4);
        return std::string(reinterpret_cast<const char *>(b.data()), 4);
    }
    [[noreturn]] void fail(const std::string &reason) const
    {
        throw ImportError(std::string(what_) + ": " + reason + " at offset " + std::to_string(position_));
    }

  private:
    ByteView data_;
    const char *what_;
    size_t position_ = 0;
};

} // namespace storm::audio_import::detail
