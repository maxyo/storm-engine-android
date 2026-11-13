// Unit tests of the DXT transcode (rebuilt mip chains, ETC2 and BC), the mip chains themselves and the disk cache on
// synthetic textures.
#include "cache.hpp"
#include "mips.hpp"
#include "service.hpp"
#include "synthetic.hpp"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <unistd.h>

using namespace storm::texture;
namespace fs = std::filesystem;

namespace
{
int failures = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                  \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

bx::DefaultAllocator allocator;

bimg::TextureFormat::Enum bimg_format(GpuFormat format)
{
    switch (format)
    {
    case GpuFormat::Etc2Rgb8:
        return bimg::TextureFormat::ETC2;
    case GpuFormat::Etc2Rgba8:
        return bimg::TextureFormat::ETC2A;
    case GpuFormat::Bc1:
        return bimg::TextureFormat::BC1;
    case GpuFormat::Bc2:
        return bimg::TextureFormat::BC2;
    case GpuFormat::Bc3:
        return bimg::TextureFormat::BC3;
    }
    return bimg::TextureFormat::Unknown;
}

struct DecodedKtx
{
    bool ok = false;
    bimg::TextureFormat::Enum format = bimg::TextureFormat::Unknown;
    uint32_t levels = 0;
    bool cube = false;
    std::vector<std::vector<uint8_t>> rgba; // [face * levels + level], block-aligned
};

// Parses a KTX blob with bimg (the parser bgfx uses) and decodes every level.
DecodedKtx decode_ktx(const Blob &blob)
{
    DecodedKtx out;
    bx::Error err;
    bimg::ImageContainer *image = bimg::imageParseKtx(&allocator, blob.data.get(), uint32_t(blob.size), &err);
    if (!image)
        return out;
    out.format = image->m_format;
    out.levels = image->m_numMips;
    out.cube = image->m_cubeMap;
    const uint32_t faces = image->m_cubeMap ? 6 : 1;
    out.ok = true;
    for (uint32_t face = 0; face < faces; ++face)
        for (uint32_t level = 0; level < out.levels; ++level)
        {
            bimg::ImageMip mip;
            if (!bimg::imageGetRawData(*image, uint16_t(face), uint8_t(level), image->m_data, image->m_size, mip))
            {
                out.ok = false;
                break;
            }
            std::vector<uint8_t> rgba(size_t(mip.m_width) * mip.m_height * 4);
            bimg::imageDecodeToRgba8(&allocator, rgba.data(), mip.m_data, mip.m_width, mip.m_height, mip.m_width * 4,
                                     mip.m_format);
            out.rgba.push_back(std::move(rgba));
        }
    bimg::imageFree(image);
    return out;
}

double psnr(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, bool with_alpha)
{
    double sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!with_alpha && i % 4 == 3)
            continue;
        const double d = double(a[i]) - b[i];
        sum += d * d;
        ++count;
    }
    const double mse = sum / count;
    return mse == 0 ? 99.0 : 10 * std::log10(255.0 * 255.0 / mse);
}

void test_decoder()
{
    uint8_t rgba[64];
    // red / blue, 4-colour mode: indices 0,1,2,3 in the first row
    const uint8_t bc1[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00}; // texels 0..3 = 0,1,2,3
    decode_level(DxtFormat::Bc1, bc1, 4, 4, rgba);
    CHECK(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
    CHECK(rgba[4] == 0 && rgba[6] == 255);
    CHECK(rgba[8] == 170 && rgba[10] == 85); // 2/3 red + 1/3 blue
    CHECK(rgba[12] == 85 && rgba[14] == 170);
    CHECK(rgba[16] == 255 && rgba[18] == 0); // texel 4 uses index 0
    CHECK(!bc1_block_has_transparency(bc1));

    // 3-colour mode (c0 <= c1): index 2 = average, index 3 = transparent black
    const uint8_t bc1a[8] = {0x1F, 0x00, 0x00, 0xF8, 0xE4, 0x00, 0x00, 0x00};
    decode_level(DxtFormat::Bc1, bc1a, 4, 4, rgba);
    CHECK(rgba[8] == 127 && rgba[10] == 127 && rgba[11] == 255); // bimg truncates
    CHECK(rgba[12] == 0 && rgba[13] == 0 && rgba[14] == 0 && rgba[15] == 0);
    CHECK(bc1_block_has_transparency(bc1a));
    const uint8_t bc1_three_opaque[8] = {0x1F, 0x00, 0x00, 0xF8, 0x24, 0x00, 0x00, 0x00}; // indices 0,1,2,0
    CHECK(!bc1_block_has_transparency(bc1_three_opaque));
    // the same colour block inside BC2/BC3 is always 4-colour
    uint8_t bc2[16] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    memcpy(bc2 + 8, bc1a, 8);
    decode_level(DxtFormat::Bc2, bc2, 4, 4, rgba);
    CHECK(rgba[3] == 0x0 * 17 && rgba[7] == 0x1 * 17 && rgba[63] == 0xF * 17);
    CHECK(rgba[12] == 170 && rgba[14] == 85); // index 3 is a colour, not transparent

    // BC3 alpha: 8-value mode (a0 > a1) and 6-value mode with 0/255
    uint8_t bc3[16] = {255, 0};
    uint64_t bits = 0;
    for (int t = 0; t < 8; ++t)
        bits |= uint64_t(t) << (3 * t);
    for (int i = 0; i < 6; ++i)
        bc3[2 + i] = uint8_t(bits >> (8 * i));
    memcpy(bc3 + 8, bc1, 8);
    decode_level(DxtFormat::Bc3, bc3, 4, 4, rgba);
    const uint8_t expect8[8] = {255, 0, 218, 182, 145, 109, 72, 36}; // bimg truncates
    for (int t = 0; t < 8; ++t)
        CHECK(rgba[t * 4 + 3] == expect8[t]);
    bc3[0] = 0, bc3[1] = 255;
    decode_level(DxtFormat::Bc3, bc3, 4, 4, rgba);
    const uint8_t expect6[8] = {0, 255, 51, 102, 153, 204, 0, 255};
    for (int t = 0; t < 8; ++t)
        CHECK(rgba[t * 4 + 3] == expect6[t]);
}

// The encoders' channel order, checked through bimg's reference decoders.
void test_channel_order()
{
    std::vector<uint8_t> image(16 * 4);
    for (int i = 0; i < 16; ++i)
        image[i * 4 + 0] = 200, image[i * 4 + 1] = 60, image[i * 4 + 2] = 10, image[i * 4 + 3] = 255;
    for (GpuFormat format : {GpuFormat::Etc2Rgb8, GpuFormat::Bc1, GpuFormat::Etc2Rgba8, GpuFormat::Bc2, GpuFormat::Bc3})
    {
        const bool alpha = format == GpuFormat::Etc2Rgba8 || format == GpuFormat::Bc2 || format == GpuFormat::Bc3;
        for (int i = 0; i < 16; ++i)
            image[i * 4 + 3] = alpha ? 90 : 255;
        uint8_t block[16];
        encode_gpu(format, image.data(), 4, 4, block);
        uint8_t decoded[64];
        bimg::imageDecodeToRgba8(&allocator, decoded, block, 4, 4, 16, bimg_format(format));
        CHECK(std::abs(decoded[0] - 200) <= 10 && std::abs(decoded[1] - 60) <= 10 && std::abs(decoded[2] - 10) <= 10);
        CHECK(std::abs(decoded[3] - (alpha ? 90 : 255)) <= 9); // BC2: 4-bit alpha
    }
}

// EAC R11 and RG11 of a derived map level through bimg's reference decoder: R and G land in the right channels,
// smooth gradients stay within a few steps, and a level that is no multiple of 4 is padded.
void test_eac()
{
    const uint32_t w = 8, h = 6;
    for (EacFormat format : {EacFormat::R11, EacFormat::Rg11})
    {
        const uint32_t channels = eac_channels(format);
        std::vector<uint8_t> texels(size_t(w) * h * channels);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
            {
                texels[(y * w + x) * channels + 0] = static_cast<uint8_t>(40 + x * 20);
                if (channels == 2)
                    texels[(y * w + x) * channels + 1] = static_cast<uint8_t>(200 - y * 25);
            }
        std::vector<uint8_t> blocks(size_t(blocks_x(w)) * blocks_x(h) * eac_block_bytes(format));
        encode_eac(format, texels.data(), w, h, blocks.data());
        std::vector<uint8_t> decoded(size_t(w) * 8 * 4);
        bimg::imageDecodeToRgba8(&allocator, decoded.data(), blocks.data(), w, 8, w * 4,
                                 format == EacFormat::R11 ? bimg::TextureFormat::EACR11 : bimg::TextureFormat::EACRG11);
        int worst = 0;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                for (uint32_t c = 0; c < channels; ++c)
                    worst = std::max(worst, std::abs(decoded[(y * w + x) * 4 + c] - texels[(y * w + x) * channels + c]));
        CHECK(worst <= 4);
        const std::vector<uint8_t> chain = encode_eac_chain(format, texels.data(), w, h, 1);
        CHECK(chain == blocks);
    }
}

// The game's uncompressed .tx formats decode to RGBA8 (R first) as the renderer uploads them.
void test_tx_formats()
{
    CHECK(tx_format_known(21) && tx_format_known(0x31545844) && !tx_format_known(50));
    CHECK(d3d_level_bytes(23, 4, 2) == 16 && d3d_level_bytes(0x31545844, 5, 5) == 32 && d3d_level_bytes(21, 2, 2) == 16);
    const uint8_t argb[4] = {10, 20, 30, 40}; // B, G, R, A in memory
    uint8_t out[4];
    CHECK(decode_rgba8(21, argb, 1, 1, out) && out[0] == 30 && out[1] == 20 && out[2] == 10 && out[3] == 40);
    const uint8_t rgb565[2] = {0x00, 0xF8}; // pure red
    CHECK(decode_rgba8(23, rgb565, 1, 1, out) && out[0] == 255 && out[1] == 0 && out[2] == 0 && out[3] == 255);
    const uint8_t argb4444[2] = {0x0F, 0x80}; // a 8, r 0, g 0, b 15
    CHECK(decode_rgba8(26, argb4444, 1, 1, out) && out[0] == 0 && out[2] == 255 && out[3] == 136);
    CHECK(!decode_rgba8(50, argb, 1, 1, out));
}

// Every value of the 16-bit formats decodes to the nearest 8-bit value (v * 255 / max, rounded), as a GPU sampling
// the native format does. The converters before bimg replicated bits instead: 14 of the 96 5- and 6-bit values came
// out one step lower; 4-bit values (times 17) and 1-bit alpha are the same either way.
void test_d3d_decode_exhaustive()
{
    std::vector<uint8_t> texels(65536 * 2);
    for (uint32_t v = 0; v < 65536; ++v)
        texels[v * 2] = uint8_t(v), texels[v * 2 + 1] = uint8_t(v >> 8);
    std::vector<uint8_t> rgba(65536 * 4);
    const auto unorm = [](uint32_t value, uint32_t max) { return uint8_t((value * 255 * 2 + max) / (2 * max)); };
    int mismatches = 0;
    for (uint32_t format : {23u, 25u, 26u})
    {
        CHECK(decode_rgba8(format, texels.data(), 256, 256, rgba.data()));
        for (uint32_t v = 0; v < 65536; ++v)
        {
            uint8_t e[4];
            if (format == 23)
                e[0] = unorm(v >> 11, 31), e[1] = unorm((v >> 5) & 63, 63), e[2] = unorm(v & 31, 31), e[3] = 255;
            else if (format == 25)
                e[0] = unorm((v >> 10) & 31, 31), e[1] = unorm((v >> 5) & 31, 31), e[2] = unorm(v & 31, 31),
                e[3] = (v & 0x8000) ? 255 : 0;
            else
                e[0] = unorm((v >> 8) & 15, 15), e[1] = unorm((v >> 4) & 15, 15), e[2] = unorm(v & 15, 15),
                e[3] = unorm(v >> 12, 15);
            mismatches += memcmp(e, &rgba[v * 4], 4) != 0;
        }
    }
    CHECK(mismatches == 0);
    const uint8_t xrgb[8] = {1, 2, 3, 0, 4, 5, 6, 77};
    uint8_t out[8];
    CHECK(decode_rgba8(22, xrgb, 2, 1, out) && out[0] == 3 && out[2] == 1 && out[3] == 255 && out[4] == 6 && out[7] == 255);
    const uint8_t abgr[4] = {9, 8, 7, 6};
    CHECK(decode_rgba8(32, abgr, 1, 1, out) && memcmp(out, abgr, 4) == 0);
}

void test_hash()
{
    // reference XXH64 values
    CHECK(hash64("", 0, 0) == 0xEF46DB3751D8E999ull);
    CHECK(hash64("abc", 3, 0) == 0x44BC2CF5AD770999ull);

    const Layout layout{DxtFormat::Bc1, 64, 32, 7, 1};
    const auto payload = test::make_payload(layout, test::Pattern::Noise, 1);
    KeyBuilder a(layout, false), b(layout, false);
    a.add(payload.data(), payload.size());
    b.add(payload.data(), payload.size());
    CHECK(a.key() == b.key());
    auto changed = payload;
    changed[payload.size() / 2] ^= 1;
    KeyBuilder c(layout, false);
    c.add(changed.data(), changed.size());
    CHECK(c.key() != a.key());
    Layout other = layout;
    other.levels = 6;
    KeyBuilder d(other, false);
    d.add(payload.data(), payload.size());
    CHECK(d.key() != a.key());
}

// The reference of a transcoded chain: the decoded top level and the chain mips.hpp builds from it, each level
// block-aligned with its edge texels repeated (as the transcode encodes them).
std::vector<std::vector<uint8_t>> reference_chain(const Layout &layout, const uint8_t *payload, uint32_t levels, bool alpha,
                                                  bool punch = false)
{
    const uint32_t aw = blocks_x(layout.width) * 4, ah = blocks_x(layout.height) * 4;
    std::vector<uint8_t> blocks(size_t(aw) * ah * 4), top(size_t(layout.width) * layout.height * 4);
    decode_level(layout.format, payload, layout.width, layout.height, blocks.data());
    for (uint32_t y = 0; y < layout.height; ++y)
        memcpy(top.data() + size_t(y) * layout.width * 4, blocks.data() + size_t(y) * aw * 4, size_t(layout.width) * 4);
    std::vector<std::vector<uint8_t>> chain{blocks};
    const auto mips = build_mip_chain(top.data(), layout.width, layout.height, levels,
                                      punch || (alpha && is_cutout_alpha(top.data(), size_t(layout.width) * layout.height)));
    for (uint32_t level = 1; level < levels; ++level)
    {
        const uint32_t w = std::max(layout.width >> level, 1u), h = std::max(layout.height >> level, 1u);
        const uint32_t bw = blocks_x(w) * 4, bh = blocks_x(h) * 4;
        std::vector<uint8_t> aligned(size_t(bw) * bh * 4);
        for (uint32_t y = 0; y < bh; ++y)
            for (uint32_t x = 0; x < bw; ++x)
                memcpy(aligned.data() + (size_t(y) * bw + x) * 4,
                       mips[level - 1].data() + (size_t(std::min(y, h - 1)) * w + std::min(x, w - 1)) * 4, 4);
        if (punch) // punch-through BC1: transparent black below the reference, opaque above
            for (size_t i = 0; i < aligned.size(); i += 4)
            {
                if (aligned[i + 3] < kCutoutReference)
                    aligned[i] = aligned[i + 1] = aligned[i + 2] = aligned[i + 3] = 0;
                else
                    aligned[i + 3] = 255;
            }
        chain.push_back(std::move(aligned));
    }
    return chain;
}

void test_quality()
{
    struct Case
    {
        DxtFormat format;
        test::Pattern pattern;
        bool punch;
        bool bc;
        GpuFormat expected;
        double min_psnr;
    };
    const Case cases[] = {
        {DxtFormat::Bc1, test::Pattern::Gradient, false, false, GpuFormat::Etc2Rgb8, 29},
        {DxtFormat::Bc1, test::Pattern::Mixed, false, false, GpuFormat::Etc2Rgb8, 33},
        {DxtFormat::Bc1, test::Pattern::Noise, false, false, GpuFormat::Etc2Rgb8, 27},
        {DxtFormat::Bc1, test::Pattern::Mixed, true, false, GpuFormat::Etc2Rgba8, 25},
        {DxtFormat::Bc3, test::Pattern::Gradient, false, false, GpuFormat::Etc2Rgba8, 29},
        {DxtFormat::Bc3, test::Pattern::Mixed, false, false, GpuFormat::Etc2Rgba8, 33},
        {DxtFormat::Bc2, test::Pattern::Mixed, false, false, GpuFormat::Etc2Rgba8, 32},
        {DxtFormat::Bc1, test::Pattern::Gradient, false, true, GpuFormat::Bc1, 29},
        {DxtFormat::Bc1, test::Pattern::Noise, false, true, GpuFormat::Bc1, 26},
        {DxtFormat::Bc1, test::Pattern::Mixed, true, true, GpuFormat::Bc1, 25},
        {DxtFormat::Bc3, test::Pattern::Mixed, false, true, GpuFormat::Bc3, 30},
        {DxtFormat::Bc2, test::Pattern::Mixed, false, true, GpuFormat::Bc2, 28},
    };
    for (const Case &tc : cases)
    {
        // The game's chain stops at 16x8 (as most of its chains stop early); the transcode completes it.
        Layout layout{tc.format, 128, 64, 4, 1};
        std::vector<uint8_t> payload;
        if (tc.format == DxtFormat::Bc2)
        {
            // BC2 = explicit 4-bit alpha + 4-colour block; build from BC3 colour blocks
            Layout bc3 = layout;
            bc3.format = DxtFormat::Bc3;
            payload = test::make_payload(bc3, tc.pattern, 7);
            for (size_t i = 0; i < payload.size(); i += 16)
                for (int j = 0; j < 8; ++j)
                    payload[i + j] = uint8_t(0x5A ^ (i + j));
        }
        else
            payload = test::make_payload(layout, tc.pattern, 7, tc.punch);
        CHECK(payload.size() == dxt_payload_size(layout));
        const GpuFormat format = choose_gpu_format(tc.format, payload.data(), payload.size(), tc.bc);
        CHECK(format == tc.expected);
        const Blob blob = transcode_to_ktx(layout, payload.data(), format);
        const Layout out = output_layout(layout);
        CHECK(out.levels == 8);
        CHECK(blob.size == ktx_size(format, out));
        const DecodedKtx ktx = decode_ktx(blob);
        CHECK(ktx.ok && ktx.levels == out.levels && !ktx.cube);
        CHECK(ktx.format == bimg_format(format));
        if (!ktx.ok)
            continue;
        if (tc.bc) // the BC tier uploads the game's top level as it is
            CHECK(memcmp(blob.data.get() + 64 + 4, payload.data(), dxt_level_size(tc.format, 128, 64)) == 0);
        const bool alpha = format != GpuFormat::Etc2Rgb8 && (format != GpuFormat::Bc1 || tc.punch);
        const auto reference =
            reference_chain(layout, payload.data(), out.levels, alpha, format == GpuFormat::Bc1 && tc.punch);
        double worst = 99;
        for (uint32_t level = 0; level < out.levels; ++level)
        {
            const double p = psnr(reference[level], ktx.rgba[level], tc.format != DxtFormat::Bc1 || tc.punch);
            // Levels 0 and 1 against the thresholds; the synthetic patterns are full-range ramps and noise at every
            // size, so the smaller levels pack ever steeper ramps into a block (no game texture does).
            if (level < 2)
                worst = std::min(worst, p);
            if (tc.punch && level == 0)
                for (size_t i = 3; i < reference[0].size(); i += 4) // binary alpha must survive exactly-ish
                    CHECK((reference[0][i] == 0) == (ktx.rgba[0][i] < 128));
        }
        printf("quality: format %d pattern %d punch %d -> gpu format %d, worst PSNR(levels 0-1) %.1f dB\n", int(tc.format),
               int(tc.pattern), int(tc.punch), int(format), worst);
        CHECK(worst >= tc.min_psnr);
    }
}

void test_cube()
{
    const Layout layout{DxtFormat::Bc1, 32, 32, 6, 6};
    const auto payload = test::make_payload(layout, test::Pattern::Gradient, 3);
    const Blob blob = transcode_to_ktx(layout, payload.data(), GpuFormat::Etc2Rgb8);
    const DecodedKtx ktx = decode_ktx(blob);
    CHECK(ktx.ok && ktx.cube && ktx.levels == 6 && ktx.rgba.size() == 36);
    if (!ktx.ok)
        return;
    // face f of the D3D payload must land on face f of the container, every level made from that face's top
    const size_t face_bytes = dxt_payload_size(layout) / 6;
    for (uint32_t face = 0; face < 6; ++face)
    {
        const auto reference = reference_chain(layout, payload.data() + face * face_bytes, 6, false);
        for (uint32_t level = 0; level < 6; ++level)
        {
            if (level < 2) // the smaller levels pack the whole ramp into one block
                CHECK(psnr(reference[level], ktx.rgba[face * 6 + level], false) > 28);
        }
    }
}

// The chains themselves: complete, averaged in linear light, weighted by alpha, coverage kept for cut-outs.
void test_mips()
{
    CHECK(full_mip_levels(1024, 1024) == 11 && full_mip_levels(512, 256) == 10 && full_mip_levels(1, 1) == 1);
    // A black and white checker of single texels: its first mip is mid grey in LINEAR light (sRGB 188), not 128.
    std::vector<uint8_t> checker(8 * 8 * 4);
    for (int i = 0; i < 64; ++i)
    {
        const uint8_t v = ((i % 8) + (i / 8)) % 2 ? 255 : 0;
        checker[i * 4] = checker[i * 4 + 1] = checker[i * 4 + 2] = v;
        checker[i * 4 + 3] = 255;
    }
    auto chain = build_mip_chain(checker.data(), 8, 8, full_mip_levels(8, 8), false);
    CHECK(chain.size() == 3 && chain[0].size() == 4 * 4 * 4 && chain[2].size() == 4);
    CHECK(std::abs(int(chain[0][0]) - 188) <= 1 && std::abs(int(chain[2][1]) - 188) <= 1 && chain[0][3] == 255);
    // Red opaque texels next to transparent green ones: the mip keeps red (no green fringe), alpha is the average.
    std::vector<uint8_t> edge(2 * 2 * 4, 0);
    edge[0] = 255, edge[3] = 255;                 // red, opaque
    edge[4 + 1] = 255, edge[8 + 1] = 255, edge[12 + 1] = 255; // green, transparent
    chain = build_mip_chain(edge.data(), 2, 2, 2, false);
    CHECK(chain[0][0] == 255 && chain[0][1] == 0 && std::abs(int(chain[0][3]) - 64) <= 1);
    // A cut-out of thin rings (a quarter of the texels pass the test).
    std::vector<uint8_t> rings(128 * 128 * 4, 128);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
        {
            const double r = std::sqrt((x - 63.5) * (x - 63.5) + (y - 63.5) * (y - 63.5)) / 17.0;
            rings[(y * 128 + x) * 4 + 3] = r - std::floor(r) < 0.25 ? 255 : 0;
        }
    CHECK(is_cutout_alpha(rings.data(), 128 * 128));
    auto coverage = [](const std::vector<uint8_t> &level) {
        size_t passed = 0;
        for (size_t i = 3; i < level.size(); i += 4)
            passed += level[i] >= kCutoutReference;
        return double(passed) / double(level.size() / 4);
    };
    const double top = coverage(rings);
    const auto plain = build_mip_chain(rings.data(), 128, 128, 5, false);
    const auto kept = build_mip_chain(rings.data(), 128, 128, 5, true);
    // Down to 16x16 (a ring a texel wide): plain averaging loses most of the rings, the kept chain stays close (texels
    // of one alpha pass or fail together, so a level matches only as finely as its alpha values allow).
    CHECK(coverage(plain[2]) < top * 0.6);
    for (int level = 0; level < 3; ++level)
        CHECK(std::abs(coverage(kept[level]) - top) < 0.05);
    // A small disk (15 % coverage, the night sky's planets): its 1x1 level stays transparent. The scaled alpha of the
    // failing texel must not round up to the reference (it did: the whole sprite drawn as an opaque square).
    std::vector<uint8_t> disk(64 * 64 * 4, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if ((x - 31.5) * (x - 31.5) + (y - 31.5) * (y - 31.5) < 14.0 * 14.0)
            {
                uint8_t *p = disk.data() + (y * 64 + x) * 4;
                p[0] = p[1] = p[2] = 200;
                p[3] = 255;
            }
    const auto disk_chain = build_mip_chain(disk.data(), 64, 64, full_mip_levels(64, 64), true);
    CHECK(coverage(disk) < 0.2 && disk_chain.back().size() == 4 && disk_chain.back()[3] < kCutoutReference);
    for (size_t level = 0; level + 4 < disk_chain.size(); ++level) // 32x32 and 16x16
        CHECK(std::abs(coverage(disk_chain[level]) - coverage(disk)) < 0.03);
    // A blended (soft) alpha is not a cut-out.
    std::vector<uint8_t> soft(16 * 16 * 4, 0);
    for (int i = 0; i < 256; ++i)
        soft[i * 4 + 3] = uint8_t(i);
    CHECK(!is_cutout_alpha(soft.data(), 256));
}

void write_bytes(const fs::path &path, const std::vector<char> &bytes)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(bytes.data(), std::streamsize(bytes.size()));
}

std::vector<char> read_bytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

void test_cache_files(const fs::path &directory)
{
    const Layout layout{DxtFormat::Bc3, 64, 64, 7, 1};
    const auto payload = test::make_payload(layout, test::Pattern::Mixed, 11);
    KeyBuilder key_builder(layout, false);
    key_builder.add(payload.data(), payload.size());
    const uint64_t key = key_builder.key();
    KeyBuilder bc_key(layout, true);
    bc_key.add(payload.data(), payload.size());
    CHECK(bc_key.key() != key); // the tiers do not share cache files
    const Blob blob = transcode_to_ktx(layout, payload.data(), GpuFormat::Etc2Rgba8);
    const Layout out = output_layout(layout);

    CHECK(!read_cache_file(directory, key, out)); // miss
    const uint64_t written = write_cache_file(directory, key, out, GpuFormat::Etc2Rgba8, blob);
    CHECK(written == sizeof(FileHeader) + blob.size);
    const fs::path path = cache_file_path(directory, key);
    CHECK(fs::file_size(path) == written);
    auto loaded = read_cache_file(directory, key, out);
    CHECK(loaded && loaded->size == blob.size && memcmp(loaded->data.get(), blob.data.get(), blob.size) == 0);
    // a different layout or key with the same file name is a miss
    Layout other = out;
    other.levels = 6;
    CHECK(!read_cache_file(directory, key, other));
    write_cache_file(directory, key, out, GpuFormat::Etc2Rgba8, blob);

    const std::vector<char> good = read_bytes(path);
    auto expect_rejected = [&](std::vector<char> bytes, const char *what) {
        write_bytes(path, bytes);
        const bool rejected = !read_cache_file(directory, key, out);
        if (!rejected)
            fprintf(stderr, "damaged cache file accepted: %s\n", what);
        CHECK(rejected);
        CHECK(!fs::exists(path)); // deleted
    };
    expect_rejected(std::vector<char>(good.begin(), good.begin() + good.size() / 2), "truncated payload");
    expect_rejected(std::vector<char>(good.begin(), good.begin() + 20), "truncated header");
    expect_rejected({}, "empty");
    auto bad = good;
    bad[0] = 'X';
    expect_rejected(bad, "magic");
    bad = good;
    bad.push_back(0);
    expect_rejected(bad, "trailing byte");
    bad = good;
    bad[offsetof(FileHeader, payload_size)] ^= 0x10;
    expect_rejected(bad, "payload size");
    bad = good;
    bad[offsetof(FileHeader, version)] ^= 1;
    expect_rejected(bad, "version");
    bad = good;
    bad[good.size() - 3] ^= 0x40;
    expect_rejected(bad, "payload bit flip");
    bad = good;
    bad[offsetof(FileHeader, gpu_format)] = 7;
    expect_rejected(bad, "format");

    // LRU eviction keeps the most recently used files
    write_bytes(path, good);
    std::vector<fs::path> files;
    for (int i = 0; i < 4; ++i)
    {
        const fs::path p = directory / ("00000000000000" + std::to_string(10 + i) + ".etc2");
        write_bytes(p, std::vector<char>(1000, 'x'));
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(10 - i));
        files.push_back(p);
    }
    write_bytes(directory / "junk.0.tmp", {'x'});
    const uint64_t total = scan_cache(directory);
    CHECK(total == good.size() + 4000);
    CHECK(!fs::exists(directory / "junk.0.tmp"));
    const uint64_t after = evict_cache(directory, good.size() + 2500);
    CHECK(after == good.size() + 2000);
    CHECK(!fs::exists(files[0]) && !fs::exists(files[1]) && fs::exists(files[2]) && fs::exists(files[3]));
    CHECK(fs::exists(path));
}

// Holds every worker of the service in a task until destroyed, so submitted jobs stay queued meanwhile. Tasks run
// only when no transcode is queued, so the workers are parked before anything is submitted.
class ParkedWorkers
{
  public:
    explicit ParkedWorkers(Service &service)
    {
        for (unsigned i = 0; i < Service::worker_count(); ++i)
            service.post([this] {
                std::unique_lock lock(mutex_);
                ++parked_;
                wake_.notify_all();
                wake_.wait(lock, [this] { return open_; });
                --parked_;
                wake_.notify_all();
            });
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return parked_ == Service::worker_count(); });
    }
    ~ParkedWorkers()
    {
        std::unique_lock lock(mutex_);
        open_ = true;
        wake_.notify_all();
        wake_.wait(lock, [this] { return parked_ == 0; }); // no task touches this object after it is gone
    }

  private:
    std::mutex mutex_;
    std::condition_variable wake_;
    unsigned parked_ = 0;
    bool open_ = false;
};

void test_service(const fs::path &directory)
{
    Service &service = Service::instance();
    Config config;
    config.mode = Mode::Etc2;
    config.directory = directory;
    service.configure(config, nullptr);
    const Layout layout{DxtFormat::Bc1, 256, 128, 9, 1};
    const auto payload = test::make_payload(layout, test::Pattern::Mixed, 21);
    KeyBuilder key_builder(layout, false);
    key_builder.add(payload.data(), payload.size());
    const uint64_t key = key_builder.key();
    CHECK(!service.lookup(key, layout));
    // Identical contents share the job while it is in flight: every worker is parked first, so the job cannot finish
    // (and leave the in-flight table) between the two submits on a loaded machine.
    std::optional<ParkedWorkers> parked(std::in_place, service);
    auto job = service.submit(layout, key, GpuFormat::Etc2Rgb8, payload);
    CHECK(job != nullptr);
    CHECK(service.submit(layout, key, GpuFormat::Etc2Rgb8, payload) == job); // deduplicated
    parked.reset();
    for (int i = 0; i < 500 && job->state.load() != Job::Done; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(job->state.load() == Job::Done && job->result && job->result->size == ktx_size(GpuFormat::Etc2Rgb8, output_layout(layout)));
    // the cache file is written before the job reports Done
    auto hit = service.lookup(key, layout);
    CHECK(hit && hit->size == job->result->size &&
          memcmp(hit->data.get(), job->result->data.get(), hit->size) == 0);
    // a cancelled job is skipped
    auto cancelled_payload = payload;
    cancelled_payload[0] ^= 1;
    service.shutdown();
    service.configure(config, nullptr);
    // Workers parked again: the jobs below stay queued, so sharing, cancelling and skipping do not depend on timing.
    parked.emplace(service);
    // shared by two holders: one release keeps it, the last one cancels it
    auto job2 = service.submit(layout, key ^ 1, GpuFormat::Etc2Rgb8, cancelled_payload);
    auto job2_shared = service.submit(layout, key ^ 1, GpuFormat::Etc2Rgb8, cancelled_payload);
    CHECK(job2_shared == job2);
    auto observer = job2;
    service.release(job2_shared);
    CHECK(!job2_shared && !observer->cancelled);
    service.release(job2);
    CHECK(observer->cancelled);
    // a cancelled job is never handed out again
    auto job3 = service.submit(layout, key ^ 1, GpuFormat::Etc2Rgb8, cancelled_payload);
    CHECK(job3 && job3 != observer && !job3->cancelled);
    service.release(job3);
    parked.reset();
    service.shutdown();
    // cancelled while still queued: skipped, it fails without a result
    CHECK(observer->state.load() == Job::Failed && !observer->result);
}
} // namespace

int main()
{
    const fs::path directory = fs::temp_directory_path() / ("storm_texture_tests_" + std::to_string(getpid()));
    fs::create_directories(directory);
    test_decoder();
    test_channel_order();
    test_eac();
    test_tx_formats();
    test_d3d_decode_exhaustive();
    test_hash();
    test_quality();
    test_cube();
    test_mips();
    test_cache_files(directory / "files");
    test_service(directory / "service");
    fs::remove_all(directory);
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("all texture tests passed\n");
    return failures ? 1 : 0;
}
