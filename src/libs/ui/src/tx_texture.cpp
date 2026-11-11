// Storm engine .tx texture: a 24-byte header (flags, width, height, mip count, D3D
// format, top mip size) followed by the mip chain. Only the top mip is decoded, by bimg.
#include "storm_ui/tx_texture.h"

#include <algorithm>

#include <bimg/bimg.h>
#include <bx/allocator.h>

namespace storm::ui
{

namespace
{

uint32_t Read32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// The .tx pixel formats (D3DFORMAT values and DXT FourCCs) as bimg formats. Every format listed
// here has a bimg decoder; X8R8G8B8 decodes as BGRA8 with the padding byte replaced by opaque alpha.
bimg::TextureFormat::Enum TxFormat(uint32_t format)
{
    switch (format)
    {
    case 0x31545844: // DXT1
        return bimg::TextureFormat::BC1;
    case 0x33545844: // DXT3
        return bimg::TextureFormat::BC2;
    case 0x35545844: // DXT5
        return bimg::TextureFormat::BC3;
    case 21: // A8R8G8B8, stored B, G, R, A
    case 22: // X8R8G8B8
        return bimg::TextureFormat::BGRA8;
    case 23: // R5G6B5
        return bimg::TextureFormat::B5G6R5;
    case 25: // A1R5G5B5
        return bimg::TextureFormat::BGR5A1;
    case 26: // A4R4G4B4
        return bimg::TextureFormat::BGRA4;
    default:
        return bimg::TextureFormat::Unknown;
    }
}

} // namespace

bool DecodeTx(const uint8_t *data, size_t size, Picture &out, std::string *error)
{
    const auto fail = [error](const char *what) {
        if (error)
            *error = what;
        return false;
    };
    if (size < 24)
        return fail("truncated header");
    const uint32_t width = Read32(data + 4), height = Read32(data + 8), format = Read32(data + 16);
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
        return fail("unreasonable size");
    const bimg::TextureFormat::Enum source = TxFormat(format);
    if (source == bimg::TextureFormat::Unknown)
        return fail("unsupported pixel format");
    const bool blocks = bimg::isCompressed(source);
    if (size - 24 < bimg::imageGetSize(nullptr, width, height, 1, false, false, 1, source))
        return fail(blocks ? "truncated DXT data" : "truncated pixel data");

    // bimg writes whole 4×4 blocks: decode into block-aligned rows, keep the visible part.
    const uint32_t decoded_width = blocks ? (width + 3) & ~3u : width;
    const uint32_t decoded_height = blocks ? (height + 3) & ~3u : height;
    std::vector<uint8_t> decoded(static_cast<size_t>(decoded_width) * decoded_height * 4);
    bx::DefaultAllocator allocator;
    bimg::imageDecodeToRgba8(&allocator, decoded.data(), data + 24, decoded_width, decoded_height, decoded_width * 4, source);

    const size_t count = static_cast<size_t>(width) * height;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    if (decoded_width == width && decoded_height == height)
        out.rgba = std::move(decoded);
    else
    {
        out.rgba.resize(count * 4);
        for (uint32_t y = 0; y < height; ++y)
            std::copy_n(decoded.begin() + static_cast<size_t>(y) * decoded_width * 4, width * 4,
                        out.rgba.begin() + static_cast<size_t>(y) * width * 4);
    }
    uint8_t *rgba = out.rgba.data();
    if (format == 22)
        for (size_t i = 0; i < count; ++i)
            rgba[i * 4 + 3] = 255;

    for (size_t i = 0; i < count; ++i) // premultiply
    {
        const uint32_t a = rgba[i * 4 + 3];
        if (a == 255)
            continue;
        for (int c = 0; c < 3; ++c)
            rgba[i * 4 + c] = static_cast<uint8_t>((rgba[i * 4 + c] * a + 127) / 255);
    }
    return true;
}

} // namespace storm::ui
