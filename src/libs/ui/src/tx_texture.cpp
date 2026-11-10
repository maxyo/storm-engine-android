// Storm engine .tx texture: a 24-byte header (flags, width, height, mip count, D3D
// format, top mip size) followed by the mip chain. Only the top mip is decoded.
#include "elements.h"

#include <cstring>

namespace storm::ui
{

namespace
{

constexpr uint32_t FourCC(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

uint32_t Read32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint16_t Read16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

void Rgb565(uint16_t c, uint8_t *rgb)
{
    rgb[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
    rgb[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
    rgb[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
}

// Colour part shared by DXT1/3/5. `punch` enables the 1-bit alpha mode of DXT1.
void ColourBlock(const uint8_t *block, bool punch, uint8_t out[16][4])
{
    const uint16_t c0 = Read16(block), c1 = Read16(block + 2);
    uint8_t palette[4][4];
    Rgb565(c0, palette[0]);
    Rgb565(c1, palette[1]);
    palette[0][3] = palette[1][3] = palette[2][3] = palette[3][3] = 255;
    if (c0 > c1 || !punch)
    {
        for (int i = 0; i < 3; ++i)
        {
            palette[2][i] = static_cast<uint8_t>((2 * palette[0][i] + palette[1][i] + 1) / 3);
            palette[3][i] = static_cast<uint8_t>((palette[0][i] + 2 * palette[1][i] + 1) / 3);
        }
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            palette[2][i] = static_cast<uint8_t>((palette[0][i] + palette[1][i]) / 2);
            palette[3][i] = 0;
        }
        palette[3][3] = 0;
    }
    const uint32_t bits = Read32(block + 4);
    for (int i = 0; i < 16; ++i)
        std::memcpy(out[i], palette[(bits >> (2 * i)) & 3], 4);
}

bool DecodeDxt(const uint8_t *data, size_t size, int width, int height, int kind, std::vector<uint8_t> &rgba)
{
    const int blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
    const size_t block_size = kind == 1 ? 8 : 16;
    if (size < static_cast<size_t>(blocks_x) * static_cast<size_t>(blocks_y) * block_size)
        return false;
    for (int by = 0; by < blocks_y; ++by)
        for (int bx = 0; bx < blocks_x; ++bx)
        {
            const uint8_t *block = data + (static_cast<size_t>(by) * blocks_x + bx) * block_size;
            uint8_t texels[16][4];
            if (kind == 1)
                ColourBlock(block, true, texels);
            else
            {
                ColourBlock(block + 8, false, texels);
                if (kind == 3)
                {
                    for (int i = 0; i < 16; ++i)
                        texels[i][3] = static_cast<uint8_t>(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
                }
                else
                {
                    uint8_t alpha[8];
                    alpha[0] = block[0];
                    alpha[1] = block[1];
                    if (alpha[0] > alpha[1])
                        for (int i = 1; i < 7; ++i)
                            alpha[i + 1] = static_cast<uint8_t>(((7 - i) * alpha[0] + i * alpha[1] + 3) / 7);
                    else
                    {
                        for (int i = 1; i < 5; ++i)
                            alpha[i + 1] = static_cast<uint8_t>(((5 - i) * alpha[0] + i * alpha[1] + 2) / 5);
                        alpha[6] = 0;
                        alpha[7] = 255;
                    }
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i)
                        bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                    for (int i = 0; i < 16; ++i)
                        texels[i][3] = alpha[(bits >> (3 * i)) & 7];
                }
            }
            for (int y = 0; y < 4 && by * 4 + y < height; ++y)
                for (int x = 0; x < 4 && bx * 4 + x < width; ++x)
                    std::memcpy(&rgba[(static_cast<size_t>(by * 4 + y) * width + bx * 4 + x) * 4], texels[y * 4 + x], 4);
        }
    return true;
}

} // namespace

bool DecodeTx(const uint8_t *data, size_t size, TxImage &out, std::string *error)
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
    const uint8_t *pixels = data + 24;
    const size_t left = size - 24;
    const size_t count = static_cast<size_t>(width) * height;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.rgba.assign(count * 4, 255);
    uint8_t *rgba = out.rgba.data();

    if (format == FourCC('D', 'X', 'T', '1') || format == FourCC('D', 'X', 'T', '3') || format == FourCC('D', 'X', 'T', '5'))
    {
        const int kind = format == FourCC('D', 'X', 'T', '1') ? 1 : format == FourCC('D', 'X', 'T', '3') ? 3 : 5;
        if (!DecodeDxt(pixels, left, out.width, out.height, kind, out.rgba))
            return fail("truncated DXT data");
    }
    else if (format == 21 || format == 22) // A8R8G8B8, X8R8G8B8 (stored B, G, R, A)
    {
        if (left < count * 4)
            return fail("truncated 32-bit data");
        for (size_t i = 0; i < count; ++i)
        {
            rgba[i * 4 + 0] = pixels[i * 4 + 2];
            rgba[i * 4 + 1] = pixels[i * 4 + 1];
            rgba[i * 4 + 2] = pixels[i * 4 + 0];
            rgba[i * 4 + 3] = format == 21 ? pixels[i * 4 + 3] : 255;
        }
    }
    else if (format == 23 || format == 25 || format == 26) // R5G6B5, A1R5G5B5, A4R4G4B4
    {
        if (left < count * 2)
            return fail("truncated 16-bit data");
        for (size_t i = 0; i < count; ++i)
        {
            const uint16_t v = Read16(pixels + i * 2);
            if (format == 23)
                Rgb565(v, rgba + i * 4);
            else if (format == 25)
            {
                rgba[i * 4 + 0] = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
                rgba[i * 4 + 1] = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
                rgba[i * 4 + 2] = static_cast<uint8_t>((v & 31) * 255 / 31);
                rgba[i * 4 + 3] = static_cast<uint8_t>((v >> 15) * 255);
            }
            else
            {
                rgba[i * 4 + 0] = static_cast<uint8_t>(((v >> 8) & 15) * 17);
                rgba[i * 4 + 1] = static_cast<uint8_t>(((v >> 4) & 15) * 17);
                rgba[i * 4 + 2] = static_cast<uint8_t>((v & 15) * 17);
                rgba[i * 4 + 3] = static_cast<uint8_t>(((v >> 12) & 15) * 17);
            }
        }
    }
    else
        return fail("unsupported pixel format");

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
