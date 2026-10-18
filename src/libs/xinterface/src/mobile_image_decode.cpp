#include "mobile_image_decode.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

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
std::array<uint8_t, 4> rgb565(uint16_t v)
{
    return {uint8_t(((v >> 11) & 31) * 255 / 31), uint8_t(((v >> 5) & 63) * 255 / 63), uint8_t((v & 31) * 255 / 31),
            255};
}
void put(Pixels &p, uint32_t x, uint32_t y, const std::array<uint8_t, 4> &c)
{
    if (x < p.width && y < p.height)
        std::copy(c.begin(), c.end(), p.rgba.begin() + (size_t(y) * p.width + x) * 4);
}
// BC blocks are always complete 8/16-byte blocks, including non-multiple-of-4 edges.
void blocks(const Reader &r, size_t offset, Pixels &p, int kind)
{
    const size_t bw = (p.width + 3) / 4, bh = (p.height + 3) / 4, stride = kind == 1 ? 8 : 16;
    r.need(offset, bw * bh * stride);
    for (size_t by = 0; by < bh; ++by)
        for (size_t bx = 0; bx < bw; ++bx)
        {
            size_t pos = offset + (by * bw + bx) * stride, color = pos + (kind == 1 ? 0 : 8);
            const auto a = r.u16(color), b = r.u16(color + 2);
            std::array<std::array<uint8_t, 4>, 4> palette{rgb565(a), rgb565(b), {}, {}};
            for (int c = 0; c < 3; ++c)
            {
                palette[2][c] = uint8_t(a > b || kind != 1 ? (2 * palette[0][c] + palette[1][c]) / 3
                                                           : (palette[0][c] + palette[1][c]) / 2);
                palette[3][c] = uint8_t(a > b || kind != 1 ? (palette[0][c] + 2 * palette[1][c]) / 3 : 0);
            }
            palette[2][3] = 255;
            palette[3][3] = (a > b || kind != 1) ? 255 : 0;
            std::array<uint8_t, 8> alpha{};
            uint64_t alphaBits = 0;
            if (kind == 5)
            {
                alpha[0] = r.u8(pos);
                alpha[1] = r.u8(pos + 1);
                if (alpha[0] > alpha[1])
                    for (int n = 2; n < 8; ++n)
                        alpha[n] = uint8_t(((8 - n) * alpha[0] + (n - 1) * alpha[1]) / 7);
                else
                {
                    for (int n = 2; n < 6; ++n)
                        alpha[n] = uint8_t(((6 - n) * alpha[0] + (n - 1) * alpha[1]) / 5);
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
                for (int n = 0; n < 6; ++n)
                    alphaBits |= uint64_t(r.u8(pos + 2 + n)) << (8 * n);
            }
            uint32_t indices = r.u32(color + 4);
            for (unsigned i = 0; i < 16; ++i)
            {
                auto c = palette[(indices >> (2 * i)) & 3];
                if (kind == 3)
                    c[3] = uint8_t(((r.u8(pos + i / 2) >> (4 * (i % 2))) & 15) * 17);
                if (kind == 5)
                    c[3] = alpha[(alphaBits >> (3 * i)) & 7];
                put(p, uint32_t(bx * 4 + i % 4), uint32_t(by * 4 + i / 4), c);
            }
        }
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
void raw(const Reader &r, size_t offset, Pixels &p, unsigned bits, size_t pitch, std::array<uint32_t, 4> masks,
         bool luminance = false)
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
            if (luminance)
                put(p, x, y, {uint8_t(v), uint8_t(v), uint8_t(v), 255});
            else
                put(p, x, y,
                    {channel(v, masks[0], 0), channel(v, masks[1], 0), channel(v, masks[2], 0),
                     channel(v, masks[3], 255)});
        }
}
constexpr uint32_t dxt1 = 0x31545844, dxt3 = 0x33545844, dxt5 = 0x35545844;
Pixels tx(const Reader &r)
{
    r.need(0, 24);
    if (r.u32(0) != 0)
        throw std::runtime_error("paletted/cubemap TX unsupported");
    auto p = allocate(r.u32(4), r.u32(8));
    const auto mips = r.u32(12), format = r.u32(16), mipSize = r.u32(20);
    if (!mips || mips > 14)
        throw std::runtime_error("invalid TX mip count");
    unsigned bits = 0;
    std::array<uint32_t, 4> masks{};
    int bc = 0;
    switch (format)
    {
    case 21:
        bits = 32;
        masks = {0xff0000, 0xff00, 0xff, 0xff000000};
        break;
    case 22:
        bits = 32;
        masks = {0xff0000, 0xff00, 0xff, 0};
        break;
    case 23:
        bits = 16;
        masks = {0xf800, 0x7e0, 0x1f, 0};
        break;
    case 25:
        bits = 16;
        masks = {0x7c00, 0x3e0, 0x1f, 0x8000};
        break;
    case 26:
        bits = 16;
        masks = {0xf00, 0xf0, 0xf, 0xf000};
        break;
    case 50:
        bits = 8;
        break;
    case dxt1:
        bc = 1;
        break;
    case dxt3:
        bc = 3;
        break;
    case dxt5:
        bc = 5;
        break;
    default:
        throw std::runtime_error("unsupported TX format");
    }
    size_t expected = bc ? size_t((p.width + 3) / 4) * ((p.height + 3) / 4) * (bc == 1 ? 8 : 16)
                         : size_t(p.width) * p.height * (bits / 8);
    if (mipSize != expected)
        throw std::runtime_error("invalid TX first mip size");
    // Validate the complete declared chain, even though the preview decodes mip zero only.
    size_t total = 24;
    uint32_t w = p.width, h = p.height;
    for (uint32_t m = 0; m < mips; ++m)
    {
        const size_t size = bc ? size_t((w + 3) / 4) * ((h + 3) / 4) * (bc == 1 ? 8 : 16) : size_t(w) * h * (bits / 8);
        r.need(total, size);
        total += size;
        if (m + 1 < mips && w == 1 && h == 1)
            throw std::runtime_error("excess TX mip levels");
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    if (bc)
        blocks(r, 24, p, bc);
    else
        raw(r, 24, p, bits, size_t(p.width) * (bits / 8), masks, format == 50);
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
        int bc = format == dxt1 ? 1 : format == dxt3 ? 3 : format == dxt5 ? 5 : 0;
        if (!bc)
            throw std::runtime_error("unsupported DDS compression");
        size_t offset = 128;
        uint32_t w = p.width, h = p.height;
        for (uint32_t m = 0; m < mips; ++m)
        {
            const size_t size = size_t((w + 3) / 4) * ((h + 3) / 4) * (bc == 1 ? 8 : 16);
            r.need(offset, size);
            offset += size;
            if (m + 1 < mips && w == 1 && h == 1)
                throw std::runtime_error("excess DDS mip levels");
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        blocks(r, 128, p, bc);
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
