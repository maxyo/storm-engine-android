#include "mobile_image_decode.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <bimg/bimg.h>
#include <bx/allocator.h>

namespace storm::mobile
{
namespace
{
struct Reader
{
    const std::vector<uint8_t> &b;
    void need(size_t p, size_t n) const
    {
        if (p > b.size() || n > b.size() - p)
            throw std::runtime_error("truncated image");
    }
    uint8_t u8(size_t p) const
    {
        need(p, 1);
        return b[p];
    }
    uint16_t u16(size_t p) const
    {
        need(p, 2);
        return uint16_t(b[p]) | uint16_t(b[p + 1]) << 8;
    }
    uint32_t u32(size_t p) const
    {
        need(p, 4);
        return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
    }
};
Pixels allocate(uint32_t w, uint32_t h)
{
    if (!w || !h || w > 8192 || h > 8192 || uint64_t(w) * h * 4 > MaxDecodedBytes)
        throw std::runtime_error("image dimensions exceed budget");
    return {w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
}
void put(Pixels &p, uint32_t x, uint32_t y, const std::array<uint8_t, 4> &c)
{
    if (x < p.width && y < p.height)
        std::copy(c.begin(), c.end(), p.rgba.begin() + (size_t(y) * p.width + x) * 4);
}
// Size of one mip level, in whole 4×4 blocks for BC formats.
size_t levelSize(bimg::TextureFormat::Enum format, uint32_t w, uint32_t h)
{
    return size_t(bimg::imageGetSize(nullptr, w, h, 1, false, false, 1, format));
}
// Validates the complete declared mip chain, even though the preview decodes mip zero only.
void mipChain(const Reader &r, size_t offset, const Pixels &p, uint32_t mips, bimg::TextureFormat::Enum format,
              const char *excess)
{
    uint32_t w = p.width, h = p.height;
    for (uint32_t m = 0; m < mips; ++m)
    {
        const size_t size = levelSize(format, w, h);
        r.need(offset, size);
        offset += size;
        if (m + 1 < mips && w == 1 && h == 1)
            throw std::runtime_error(excess);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
}
// Mip zero by bimg (BC1/2/3 and the TX raw formats). bimg writes whole 4×4 blocks, so BC images
// with edges that are not multiples of 4 decode into block-aligned rows and are then cropped.
void decode(const Reader &r, size_t offset, Pixels &p, bimg::TextureFormat::Enum format)
{
    r.need(offset, levelSize(format, p.width, p.height));
    const bool blocks = bimg::isCompressed(format);
    const uint32_t w = blocks ? (p.width + 3) & ~3u : p.width, h = blocks ? (p.height + 3) & ~3u : p.height;
    bx::DefaultAllocator allocator;
    if (w == p.width && h == p.height)
    {
        bimg::imageDecodeToRgba8(&allocator, p.rgba.data(), r.b.data() + offset, w, h, w * 4, format);
        return;
    }
    std::vector<uint8_t> full(size_t(w) * h * 4);
    bimg::imageDecodeToRgba8(&allocator, full.data(), r.b.data() + offset, w, h, w * 4, format);
    for (uint32_t y = 0; y < p.height; ++y)
        std::copy_n(full.begin() + size_t(y) * w * 4, size_t(p.width) * 4, p.rgba.begin() + size_t(y) * p.width * 4);
}
uint8_t channel(uint32_t v, uint32_t mask, uint8_t fallback)
{
    if (!mask)
        return fallback;
    unsigned shift = 0;
    while (!(mask & 1))
    {
        mask >>= 1;
        ++shift;
    }
    if ((mask & (mask + 1)) != 0)
        throw std::runtime_error("non-contiguous pixel mask");
    return uint8_t((uint64_t((v >> shift) & mask) * 255) / mask);
}
// Uncompressed DDS with arbitrary channel masks and an explicit row pitch.
void raw(const Reader &r, size_t offset, Pixels &p, unsigned bits, size_t pitch, std::array<uint32_t, 4> masks)
{
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
        throw std::runtime_error("unsupported pixel depth");
    const size_t row = size_t(p.width) * (bits / 8);
    if (pitch < row || pitch > MaxEncodedBytes)
        throw std::runtime_error("invalid row pitch");
    r.need(offset, pitch * (p.height - 1) + row);
    for (uint32_t y = 0; y < p.height; ++y)
        for (uint32_t x = 0; x < p.width; ++x)
        {
            const size_t pos = offset + size_t(y) * pitch + x * (bits / 8);
            uint32_t v = 0;
            for (unsigned n = 0; n < bits / 8; ++n)
                v |= uint32_t(r.u8(pos + n)) << (8 * n);
            put(p, x, y,
                {channel(v, masks[0], 0), channel(v, masks[1], 0), channel(v, masks[2], 0), channel(v, masks[3], 255)});
        }
}
constexpr uint32_t dxt1 = 0x31545844, dxt3 = 0x33545844, dxt5 = 0x35545844;
bimg::TextureFormat::Enum bcFormat(uint32_t fourcc)
{
    switch (fourcc)
    {
    case dxt1:
        return bimg::TextureFormat::BC1;
    case dxt3:
        return bimg::TextureFormat::BC2;
    case dxt5:
        return bimg::TextureFormat::BC3;
    default:
        return bimg::TextureFormat::Unknown;
    }
}
// TX D3DFORMAT values as bimg formats. X8R8G8B8 and L8 are fixed up after decoding (opaque alpha;
// bimg's R8 leaves green and blue zero).
bimg::TextureFormat::Enum txFormat(uint32_t format)
{
    switch (format)
    {
    case 21: // A8R8G8B8
    case 22: // X8R8G8B8
        return bimg::TextureFormat::BGRA8;
    case 23: // R5G6B5
        return bimg::TextureFormat::B5G6R5;
    case 25: // A1R5G5B5
        return bimg::TextureFormat::BGR5A1;
    case 26: // A4R4G4B4
        return bimg::TextureFormat::BGRA4;
    case 50: // L8
        return bimg::TextureFormat::R8;
    default:
        return bcFormat(format);
    }
}
Pixels tx(const Reader &r)
{
    r.need(0, 24);
    if (r.u32(0) != 0)
        throw std::runtime_error("paletted/cubemap TX unsupported");
    auto p = allocate(r.u32(4), r.u32(8));
    const auto mips = r.u32(12), format = r.u32(16), mipSize = r.u32(20);
    if (!mips || mips > 14)
        throw std::runtime_error("invalid TX mip count");
    const auto source = txFormat(format);
    if (source == bimg::TextureFormat::Unknown)
        throw std::runtime_error("unsupported TX format");
    if (mipSize != levelSize(source, p.width, p.height))
        throw std::runtime_error("invalid TX first mip size");
    mipChain(r, 24, p, mips, source, "excess TX mip levels");
    decode(r, 24, p, source);
    for (size_t i = 0; i < p.rgba.size(); i += 4)
        if (format == 22)
            p.rgba[i + 3] = 255;
        else if (format == 50)
            p.rgba[i + 1] = p.rgba[i + 2] = p.rgba[i];
    return p;
}
Pixels dds(const Reader &r)
{
    r.need(0, 128);
    if (r.u32(4) != 124 || r.u32(76) != 32)
        throw std::runtime_error("invalid DDS header");
    if (r.u32(112) != 0 || r.u32(24) > 1)
        throw std::runtime_error("DDS cubemap/volume unsupported");
    auto p = allocate(r.u32(16), r.u32(12));
    const auto flags = r.u32(80), format = r.u32(84);
    const auto mips = std::max(1u, r.u32(28));
    if (mips > 14)
        throw std::runtime_error("invalid DDS mip count");
    if (flags & 4)
    {
        const auto source = bcFormat(format);
        if (source == bimg::TextureFormat::Unknown)
            throw std::runtime_error("unsupported DDS compression");
        mipChain(r, 128, p, mips, source, "excess DDS mip levels");
        decode(r, 128, p, source);
    }
    else if (flags & 0x40)
    {
        if (mips > 1)
            throw std::runtime_error("uncompressed DDS mip chains unsupported");
        const unsigned bits = r.u32(88);
        const size_t pitch = (r.u32(8) & 8) ? r.u32(20) : size_t(p.width) * (bits / 8);
        const std::array<uint32_t, 4> masks{r.u32(92), r.u32(96), r.u32(100), r.u32(104)};
        if (!masks[0] || !masks[1] || !masks[2])
            throw std::runtime_error("missing DDS RGB masks");
        uint32_t used = 0;
        for (auto mask : masks)
        {
            if (mask & used || (bits < 32 && (uint64_t(mask) >> bits)))
                throw std::runtime_error("invalid DDS masks");
            used |= mask;
        }
        raw(r, 128, p, bits, pitch, masks);
    }
    else
        throw std::runtime_error("unsupported DDS pixel format");
    return p;
}
Pixels tga(const Reader &r)
{
    r.need(0, 18);
    const auto type = r.u8(2), bits = r.u8(16), desc = r.u8(17);
    if (r.u8(1) != 0 || (type != 2 && type != 10) || (bits != 24 && bits != 32) || (desc & 0xc0))
        throw std::runtime_error("unsupported TGA format");
    auto p = allocate(r.u16(12), r.u16(14));
    size_t pos = 18 + r.u8(0), n = 0, total = size_t(p.width) * p.height;
    auto pixel = [&] {
        r.need(pos, bits / 8);
        std::array<uint8_t, 4> c{r.u8(pos + 2), r.u8(pos + 1), r.u8(pos), bits == 32 ? r.u8(pos + 3) : uint8_t(255)};
        pos += bits / 8;
        return c;
    };
    auto emit = [&](const auto &c) {
        const uint32_t x = uint32_t(n % p.width), y = uint32_t(n / p.width);
        put(p, (desc & 16) ? p.width - 1 - x : x, (desc & 32) ? y : p.height - 1 - y, c);
        ++n;
    };
    while (n < total)
    {
        if (type == 2)
        {
            emit(pixel());
            continue;
        }
        const auto code = r.u8(pos++);
        const size_t count = (code & 127) + 1;
        if (count > total - n)
            throw std::runtime_error("TGA RLE exceeds pixels");
        if (code & 128)
        {
            auto c = pixel();
            for (size_t i = 0; i < count; ++i)
                emit(c);
        }
        else
            for (size_t i = 0; i < count; ++i)
                emit(pixel());
    }
    return p;
}
} // namespace
DecodeResult Decode(const std::vector<uint8_t> &bytes, const std::string &extension)
{
    try
    {
        if (bytes.size() > MaxEncodedBytes)
            throw std::runtime_error("encoded image exceeds budget");
        Reader r{bytes};
        if (extension == "tx")
            return {tx(r), {}};
        if (bytes.size() >= 4 && r.u32(0) == 0x20534444)
            return {dds(r), {}};
        return {tga(r), {}};
    }
    catch (const std::exception &e)
    {
        return {{}, e.what()};
    }
}
DecodeResult CropResize(const Pixels &src, Crop uv, uint32_t maxEdge)
{
    try
    {
        if (!src.width || !src.height || src.rgba.size() != uint64_t(src.width) * src.height * 4)
            throw std::runtime_error("invalid decoded pixels");
        if (!maxEdge || maxEdge > 512)
            throw std::runtime_error("invalid requested edge");
        for (float v : {uv.left, uv.top, uv.right, uv.bottom})
            if (!std::isfinite(v) || v < 0 || v > 1)
                throw std::runtime_error("crop outside texture");
        if (uv.left == uv.right || uv.top == uv.bottom)
            throw std::runtime_error("empty crop");
        // UV edges describe pixel coverage. Floor/ceil also preserve atlas half-texel inset edges.
        const auto left = uint32_t(std::floor(std::min(uv.left, uv.right) * src.width));
        const auto top = uint32_t(std::floor(std::min(uv.top, uv.bottom) * src.height));
        const auto right = std::min(src.width, uint32_t(std::ceil(std::max(uv.left, uv.right) * src.width)));
        const auto bottom = std::min(src.height, uint32_t(std::ceil(std::max(uv.top, uv.bottom) * src.height)));
        if (right <= left || bottom <= top)
            throw std::runtime_error("empty pixel crop");
        const auto w = right - left, h = bottom - top;
        const double scale = std::min(1., double(maxEdge) / std::max(w, h));
        auto dst =
            allocate(std::max(1u, uint32_t(std::lround(w * scale))), std::max(1u, uint32_t(std::lround(h * scale))));
        for (uint32_t y = 0; y < dst.height; ++y)
            for (uint32_t x = 0; x < dst.width; ++x)
            {
                uint32_t sx = std::min(w - 1, uint32_t((uint64_t(2 * x + 1) * w) / (2 * dst.width))),
                         sy = std::min(h - 1, uint32_t((uint64_t(2 * y + 1) * h) / (2 * dst.height)));
                sx = uv.right > uv.left ? left + sx : right - 1 - sx;
                sy = uv.bottom > uv.top ? top + sy : bottom - 1 - sy;
                const auto off = (size_t(sy) * src.width + sx) * 4;
                std::copy_n(src.rgba.begin() + off, 4, dst.rgba.begin() + (size_t(y) * dst.width + x) * 4);
            }
        return {std::move(dst), {}};
    }
    catch (const std::exception &e)
    {
        return {{}, e.what()};
    }
}
} // namespace storm::mobile
