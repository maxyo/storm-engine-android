// Unit tests of the non-.tx texture files (storm/render/texture_file.hpp) on synthetic files:
// DDS (legacy FourCC, DX10, sRGB, cube maps, a CPU-decoded format), TXX and the name rule.
//
// Optional corpus check (local only, never in CI): STORM_TEXTURE_CORPUS=<game Resource dir>
// parses the header and mip chain layout of every .dds and .txx below it and prints the counts
// per source format.
#include "storm/render/texture_file.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

using namespace storm::render;

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

void put32(std::vector<uint8_t> &out, size_t offset, uint32_t value) { memcpy(out.data() + offset, &value, 4); }
uint32_t fourcc(const char *code) { return uint32_t(code[0]) | uint32_t(code[1]) << 8 | uint32_t(code[2]) << 16 | uint32_t(code[3]) << 24; }

struct DdsSpec
{
    uint32_t width = 8, height = 8, levels = 1;
    const char *fourcc = "DXT1";
    uint32_t dxgi = 0; // with fourcc "DX10"
    bool cube = false;
    uint32_t block_bytes = 8; // 0: 32-bit uncompressed (A8R8G8B8 masks)
};

// Writes a DDS file; texel data is a byte counter so every subresource is distinguishable.
std::vector<uint8_t> make_dds(const DdsSpec &spec, size_t *payload_offset = nullptr)
{
    const bool dx10 = spec.dxgi != 0;
    const size_t header = 4 + 124 + (dx10 ? 20 : 0);
    std::vector<uint8_t> out(header);
    memcpy(out.data(), "DDS ", 4);
    put32(out, 4, 124);
    put32(out, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000); // caps, height, width, pixel format, mip count
    put32(out, 12, spec.height);
    put32(out, 16, spec.width);
    put32(out, 28, spec.levels);
    put32(out, 76, 32);
    if (spec.block_bytes)
    {
        put32(out, 80, 0x4); // DDPF_FOURCC
        put32(out, 84, fourcc(spec.fourcc));
    }
    else
    {
        put32(out, 80, 0x41); // DDPF_RGB | DDPF_ALPHAPIXELS
        put32(out, 88, 32);
        put32(out, 92, 0x00ff0000);
        put32(out, 96, 0x0000ff00);
        put32(out, 100, 0x000000ff);
        put32(out, 104, 0xff000000);
    }
    put32(out, 108, 0x1000 | (spec.levels > 1 ? 0x400008 : 0)); // texture, mipmap|complex
    put32(out, 112, spec.cube ? 0x200 | 0xfc00 : 0);
    if (dx10)
    {
        put32(out, 128, spec.dxgi);
        put32(out, 132, 3); // texture 2D
        put32(out, 136, spec.cube ? 4 : 0);
        put32(out, 140, 1);
    }
    if (payload_offset)
        *payload_offset = header;
    uint8_t counter = 0;
    for (uint32_t face = 0; face < (spec.cube ? 6u : 1u); ++face)
        for (uint32_t level = 0; level < spec.levels; ++level)
        {
            const uint32_t w = std::max(1u, spec.width >> level), h = std::max(1u, spec.height >> level);
            const size_t size = spec.block_bytes ? size_t((w + 3) / 4) * ((h + 3) / 4) * spec.block_bytes : size_t(w) * h * 4;
            for (size_t i = 0; i < size; ++i)
                out.push_back(counter++);
        }
    return out;
}

void test_dds_legacy()
{
    size_t payload = 0;
    TextureFile file;
    std::vector<uint8_t> bytes = make_dds({.width = 16, .height = 8, .levels = 5, .fourcc = "DXT1"}, &payload);
    CHECK(DetectTextureContainer(bytes) == TextureContainer::Dds);
    const std::vector<uint8_t> copy = bytes;
    CHECK(ParseDdsTexture(std::move(bytes), file).empty());
    CHECK(file.format == TextureFileFormat::Dxt1 && !file.decoded && !file.srgb);
    CHECK(file.width == 16 && file.height == 8 && file.levels == 5 && file.faces == 1);
    // 16x8 -> 4x2 blocks, 8x4 -> 2x1, 4x2 -> 1, 2x1 -> 1, 1x1 -> 1
    const size_t sizes[5] = {64, 16, 8, 8, 8};
    size_t offset = payload;
    for (uint32_t level = 0; level < 5; ++level)
    {
        CHECK(file.level(0, level).size == sizes[level]);
        CHECK(file.level(0, level).offset == offset);
        CHECK(memcmp(file.data(0, level).data(), copy.data() + offset, sizes[level]) == 0);
        offset += sizes[level];
    }
    CHECK(file.level(0, 3).width == 2 && file.level(0, 3).height == 1);

    CHECK(ParseDdsTexture(make_dds({.fourcc = "DXT3", .block_bytes = 16}), file).empty() && file.format == TextureFileFormat::Dxt3);
    CHECK(ParseDdsTexture(make_dds({.fourcc = "DXT5", .block_bytes = 16}), file).empty() && file.format == TextureFileFormat::Dxt5);
}

void test_dds_dx10_srgb_cube()
{
    TextureFile file;
    // DXGI_FORMAT_BC3_UNORM_SRGB = 78, cube map with 3 levels
    std::vector<uint8_t> bytes = make_dds({.width = 8, .height = 8, .levels = 3, .fourcc = "DX10", .dxgi = 78, .cube = true, .block_bytes = 16});
    CHECK(ParseDdsTexture(std::move(bytes), file).empty());
    CHECK(file.format == TextureFileFormat::Dxt5 && file.srgb && file.faces == 6 && file.levels == 3);
    CHECK(file.subresources.size() == 18);
    // face-major: face 1 starts after the 4 + 1 + 1 blocks of face 0
    CHECK(file.level(1, 0).offset == file.level(0, 0).offset + 6 * 16);
    CHECK(file.source.find("sRGB") != std::string::npos && file.source.find("cube") != std::string::npos);
    // DXGI_FORMAT_BC1_UNORM = 71, DXGI_FORMAT_BC2_UNORM_SRGB = 75
    CHECK(ParseDdsTexture(make_dds({.fourcc = "DX10", .dxgi = 71}), file).empty() && file.format == TextureFileFormat::Dxt1 && !file.srgb);
    CHECK(ParseDdsTexture(make_dds({.fourcc = "DX10", .dxgi = 75, .block_bytes = 16}), file).empty() && file.format == TextureFileFormat::Dxt3 && file.srgb);
}

void test_dds_decoded()
{
    TextureFile file;
    // BC4 (DXGI 80): one 8-byte block per 4x4; a 2x2 image keeps 2x2 decoded texels.
    std::vector<uint8_t> bytes = make_dds({.width = 2, .height = 2, .levels = 1, .fourcc = "DX10", .dxgi = 80});
    // red endpoints 200 / 100, every texel index 0 -> red 200
    const size_t payload = bytes.size() - 8;
    const uint8_t block[8] = {200, 100, 0, 0, 0, 0, 0, 0};
    memcpy(bytes.data() + payload, block, 8);
    CHECK(ParseDdsTexture(std::move(bytes), file).empty());
    CHECK(file.format == TextureFileFormat::Bgra8 && file.decoded);
    CHECK(file.level(0, 0).size == 2 * 2 * 4 && file.storage.size() == 16);
    const uint8_t *bgra = file.data(0, 0).data();
    CHECK(bgra[2] == 200 && bgra[3] == 255 && bgra[14] == 200);

    // uncompressed A8R8G8B8 through bimg's pixel-format table
    bytes = make_dds({.width = 4, .height = 2, .levels = 2, .fourcc = "", .block_bytes = 0});
    CHECK(ParseDdsTexture(std::move(bytes), file).empty());
    CHECK(file.format == TextureFileFormat::Bgra8 && file.levels == 2 && file.level(0, 1).size == 2 * 1 * 4);
}

void test_dds_rejects()
{
    TextureFile file;
    std::vector<uint8_t> bytes = make_dds({.width = 64, .height = 64, .levels = 4});
    bytes.resize(bytes.size() - 1);
    CHECK(!ParseDdsTexture(bytes, file).empty()); // truncated mip chain
    bytes = make_dds({});
    put32(bytes, 84, fourcc("ABCD"));
    CHECK(!ParseDdsTexture(bytes, file).empty()); // unknown format
    CHECK(!ParseDdsTexture({'D', 'D', 'S', ' ', 1, 2}, file).empty());
    CHECK(!ParseDdsTexture(make_dds({.width = 8, .height = 4, .cube = true}), file).empty()); // non-square cube
}

std::vector<uint8_t> make_txx(int32_t format, int32_t size, int32_t levels, int32_t bpp)
{
    std::vector<uint8_t> out(44);
    memcpy(out.data(), "TXX 1.0 ", 8);
    const int32_t fields[9] = {1, format, size, size, 1, levels, size * bpp / 8, bpp, 0};
    memcpy(out.data() + 8, fields, sizeof(fields));
    size_t payload = 0;
    for (int32_t level = 0; level < levels; ++level)
    {
        const int32_t w = std::max(1, size >> level);
        payload += size_t((w + 3) / 4) * ((w + 3) / 4) * (bpp * 2);
    }
    out.resize(44 + payload, 0x5a);
    return out;
}

void test_txx()
{
    TextureFile file;
    std::vector<uint8_t> bytes = make_txx(11, 64, 7, 8);
    CHECK(DetectTextureContainer(bytes) == TextureContainer::Txx);
    CHECK(ParseTxxTexture(bytes, file).empty());
    CHECK(file.format == TextureFileFormat::Dxt5 && file.levels == 7 && file.width == 64);
    CHECK(file.level(0, 0).offset == 44 && file.level(0, 0).size == 16 * 16 * 16);
    CHECK(file.level(0, 6).size == 16); // 1x1
    CHECK(ParseTxxTexture(make_txx(9, 32, 6, 4), file).empty() && file.format == TextureFileFormat::Dxt1);

    bytes.pop_back();
    CHECK(!ParseTxxTexture(bytes, file).empty()); // size mismatch
    CHECK(!ParseTxxTexture(make_txx(5, 32, 1, 16), file).empty()); // unknown format
    CHECK(!ParseTxxTexture(make_txx(11, 32, 1, 4), file).empty()); // bpp does not match the format
}

void test_names()
{
    auto c = GetTextureFileCandidates("LOCATIONS\\FOO.TGA");
    CHECK(c.primary == "resource\\textures\\LOCATIONS\\FOO.TGA.tx");
    // the .tga.tx alternate equals the primary (case-insensitive) and is skipped
    CHECK(c.alternates == (std::vector<std::string>{"resource\\textures\\LOCATIONS\\FOO.dds",
                                                     "resource\\textures\\LOCATIONS\\FOO.txx"}));
    c = GetTextureFileCandidates("Location\\Store02_AO.dds");
    CHECK(c.primary == "resource\\textures\\Location\\Store02_AO.dds");
    CHECK(c.alternates == (std::vector<std::string>{"resource\\textures\\Location\\Store02_AO.tga.tx",
                                                     "resource\\textures\\Location\\Store02_AO.txx"}));
    c = GetTextureFileCandidates("Resource\\Textures\\\\blood_nom.dds");
    CHECK(c.primary == "Resource\\Textures\\blood_nom.dds");
    c = GetTextureFileCandidates("particles_newnm");
    CHECK(c.primary == "resource\\textures\\particles_newnm.tx");
    CHECK(c.alternates.size() == 3 && c.alternates[2] == "resource\\textures\\particles_newnm.txx");
    c = GetTextureFileCandidates("Particles_New.txx");
    CHECK(c.primary == "resource\\textures\\Particles_New.txx" && c.alternates.size() == 2);
    c = GetTextureFileCandidates("weather/sun/flare.tga.tx");
    CHECK(c.primary == "resource\\textures\\weather\\sun\\flare.tga.tx");
    CHECK(c.alternates.size() == 2 && c.alternates[0] == "resource\\textures\\weather\\sun\\flare.dds");
}

// Local-only: parse every .dds / .txx under a game's Resource directory.
int corpus(const char *root)
{
    namespace fs = std::filesystem;
    const auto started = std::chrono::steady_clock::now();
    std::map<std::string, int> ok;
    std::map<std::string, int> failed;
    int total = 0, bad = 0;
    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        std::string ext = entry.path().extension().string();
        for (char &ch : ext)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (ext != ".dds" && ext != ".txx")
            continue;
        // Headers and the mip chain layout are what is checked; the opaque block payload is not
        // read (zeros stand in for it) so the whole corpus takes seconds instead of reading ~7 GB.
        std::vector<uint8_t> bytes(entry.file_size());
        std::ifstream in(entry.path(), std::ios::binary);
        in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(std::min<size_t>(bytes.size(), 4096)));
        TextureFile file;
        const std::string error = ext == ".dds" ? ParseDdsTexture(std::move(bytes), file) : ParseTxxTexture(std::move(bytes), file);
        ++total;
        if (error.empty())
            ++ok[file.source + (file.decoded ? " (decoded)" : "")];
        else
        {
            ++bad;
            if (failed[error]++ < 3)
                printf("FAIL %s: %s\n", entry.path().c_str(), error.c_str());
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    printf("corpus %s: %d files, %d parsed, %d failed, %.1f s\n", root, total, total - bad, bad, seconds);
    for (const auto &[name, count] : ok)
        printf("  ok   %6d  %s\n", count, name.c_str());
    for (const auto &[name, count] : failed)
        printf("  fail %6d  %s\n", count, name.c_str());
    return bad ? 1 : 0;
}
} // namespace

int main()
{
    test_dds_legacy();
    test_dds_dx10_srgb_cube();
    test_dds_decoded();
    test_dds_rejects();
    test_txx();
    test_names();
    if (failures)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("storm_texture_format_tests: all checks passed\n");
    if (const char *root = getenv("STORM_TEXTURE_CORPUS"))
        return corpus(root);
    return 0;
}
