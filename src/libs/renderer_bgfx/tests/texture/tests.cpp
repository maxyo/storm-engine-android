// Unit tests of the DXT -> ETC2 transcode and its disk cache on synthetic textures.
#include "cache.hpp"
#include "service.hpp"
#include "synthetic.hpp"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>
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

// etcpak's pixel convention, checked through bimg's reference ETC2 decoder.
void test_channel_order()
{
    std::vector<uint8_t> image(16 * 4);
    for (int i = 0; i < 16; ++i)
        image[i * 4 + 0] = 200, image[i * 4 + 1] = 60, image[i * 4 + 2] = 10, image[i * 4 + 3] = 255;
    for (EtcFormat format : {EtcFormat::Rgb8, EtcFormat::Rgba8})
    {
        if (format == EtcFormat::Rgba8)
            for (int i = 0; i < 16; ++i)
                image[i * 4 + 3] = 90;
        uint8_t block[16];
        EncodeScratch scratch;
        encode_level(format, image.data(), 4, 4, block, scratch);
        uint8_t decoded[64];
        bimg::imageDecodeToRgba8(&allocator, decoded, block, 4, 4, 16,
                                 format == EtcFormat::Rgb8 ? bimg::TextureFormat::ETC2 : bimg::TextureFormat::ETC2A);
        CHECK(std::abs(decoded[0] - 200) <= 10 && std::abs(decoded[1] - 60) <= 10 && std::abs(decoded[2] - 10) <= 10);
        CHECK(std::abs(decoded[3] - (format == EtcFormat::Rgb8 ? 255 : 90)) <= 2);
    }
}

void test_hash()
{
    // reference XXH64 values
    CHECK(hash64("", 0, 0) == 0xEF46DB3751D8E999ull);
    CHECK(hash64("abc", 3, 0) == 0x44BC2CF5AD770999ull);

    const Layout layout{DxtFormat::Bc1, 64, 32, 7, 1};
    const auto payload = test::make_payload(layout, test::Pattern::Noise, 1);
    KeyBuilder a(layout), b(layout);
    a.add(payload.data(), payload.size());
    b.add(payload.data(), payload.size());
    CHECK(a.key() == b.key());
    auto changed = payload;
    changed[payload.size() / 2] ^= 1;
    KeyBuilder c(layout);
    c.add(changed.data(), changed.size());
    CHECK(c.key() != a.key());
    Layout other = layout;
    other.levels = 6;
    KeyBuilder d(other);
    d.add(payload.data(), payload.size());
    CHECK(d.key() != a.key());
}

void test_quality()
{
    struct Case
    {
        DxtFormat format;
        test::Pattern pattern;
        bool punch;
        EtcFormat expected;
        double min_psnr;
    };
    const Case cases[] = {
        {DxtFormat::Bc1, test::Pattern::Gradient, false, EtcFormat::Rgb8, 29},
        {DxtFormat::Bc1, test::Pattern::Mixed, false, EtcFormat::Rgb8, 33},
        {DxtFormat::Bc1, test::Pattern::Noise, false, EtcFormat::Rgb8, 27},
        {DxtFormat::Bc1, test::Pattern::Mixed, true, EtcFormat::Rgba8, 25},
        {DxtFormat::Bc3, test::Pattern::Gradient, false, EtcFormat::Rgba8, 29},
        {DxtFormat::Bc3, test::Pattern::Mixed, false, EtcFormat::Rgba8, 33},
        {DxtFormat::Bc2, test::Pattern::Mixed, false, EtcFormat::Rgba8, 32},
    };
    for (const Case &tc : cases)
    {
        Layout layout{tc.format, 128, 64, 8, 1}; // down to 1x1 (levels smaller than a block)
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
        const EtcFormat format = choose_etc_format(tc.format, payload.data(), payload.size());
        CHECK(format == tc.expected);
        const Blob blob = transcode_to_ktx(layout, payload.data(), format);
        CHECK(blob.size == ktx_size(format, layout));
        const DecodedKtx ktx = decode_ktx(blob);
        CHECK(ktx.ok && ktx.levels == layout.levels && !ktx.cube);
        CHECK(ktx.format == (format == EtcFormat::Rgb8 ? bimg::TextureFormat::ETC2 : bimg::TextureFormat::ETC2A));
        if (!ktx.ok)
            continue;
        size_t offset = 0;
        double worst = 99;
        for (uint32_t level = 0; level < layout.levels; ++level)
        {
            const uint32_t w = layout.level_width(level), h = layout.level_height(level);
            std::vector<uint8_t> reference(size_t(blocks_x(w)) * 4 * blocks_x(h) * 4 * 4);
            decode_level(tc.format, payload.data() + offset, w, h, reference.data());
            offset += dxt_level_size(tc.format, w, h);
            const double p = psnr(reference, ktx.rgba[level], tc.format != DxtFormat::Bc1 || tc.punch);
            if (level < 4)
                worst = std::min(worst, p);
            if (tc.punch)
                for (size_t i = 3; i < reference.size(); i += 4) // binary alpha must survive exactly-ish
                    CHECK((reference[i] == 0) == (ktx.rgba[level][i] < 128));
        }
        printf("quality: format %d pattern %d punch %d -> %s, worst PSNR(levels 0-3) %.1f dB\n", int(tc.format),
               int(tc.pattern), int(tc.punch), format == EtcFormat::Rgb8 ? "ETC2 RGB8" : "ETC2 RGBA8", worst);
        CHECK(worst >= tc.min_psnr);
    }
}

void test_cube()
{
    const Layout layout{DxtFormat::Bc1, 32, 32, 6, 6};
    const auto payload = test::make_payload(layout, test::Pattern::Gradient, 3);
    const Blob blob = transcode_to_ktx(layout, payload.data(), EtcFormat::Rgb8);
    const DecodedKtx ktx = decode_ktx(blob);
    CHECK(ktx.ok && ktx.cube && ktx.levels == 6 && ktx.rgba.size() == 36);
    if (!ktx.ok)
        return;
    // face f level l of the D3D payload must land on face f level l of the container
    size_t offset = 0;
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t level = 0; level < 6; ++level)
        {
            const uint32_t w = layout.level_width(level), h = layout.level_height(level);
            std::vector<uint8_t> reference(size_t(blocks_x(w)) * 4 * blocks_x(h) * 4 * 4);
            decode_level(layout.format, payload.data() + offset, w, h, reference.data());
            offset += dxt_level_size(layout.format, w, h);
            CHECK(psnr(reference, ktx.rgba[face * 6 + level], false) > 28);
        }
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
    KeyBuilder key_builder(layout);
    key_builder.add(payload.data(), payload.size());
    const uint64_t key = key_builder.key();
    const Blob blob = transcode_to_ktx(layout, payload.data(), EtcFormat::Rgba8);

    CHECK(!read_cache_file(directory, key, layout)); // miss
    const uint64_t written = write_cache_file(directory, key, layout, EtcFormat::Rgba8, blob);
    CHECK(written == sizeof(FileHeader) + blob.size);
    const fs::path path = cache_file_path(directory, key);
    CHECK(fs::file_size(path) == written);
    auto loaded = read_cache_file(directory, key, layout);
    CHECK(loaded && loaded->size == blob.size && memcmp(loaded->data.get(), blob.data.get(), blob.size) == 0);
    // a different layout or key with the same file name is a miss
    Layout other = layout;
    other.levels = 6;
    CHECK(!read_cache_file(directory, key, other));
    write_cache_file(directory, key, layout, EtcFormat::Rgba8, blob);

    const std::vector<char> good = read_bytes(path);
    auto expect_rejected = [&](std::vector<char> bytes, const char *what) {
        write_bytes(path, bytes);
        const bool rejected = !read_cache_file(directory, key, layout);
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
    bad[offsetof(FileHeader, etc_format)] = 7;
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

void test_service(const fs::path &directory)
{
    Service &service = Service::instance();
    Config config;
    config.mode = Mode::Etc2;
    config.directory = directory;
    service.configure(config, nullptr);
    const Layout layout{DxtFormat::Bc1, 256, 128, 9, 1};
    const auto payload = test::make_payload(layout, test::Pattern::Mixed, 21);
    KeyBuilder key_builder(layout);
    key_builder.add(payload.data(), payload.size());
    const uint64_t key = key_builder.key();
    CHECK(!service.lookup(key, layout));
    auto job = service.submit(layout, key, EtcFormat::Rgb8, payload);
    CHECK(job != nullptr);
    CHECK(service.submit(layout, key, EtcFormat::Rgb8, payload) == job); // deduplicated
    for (int i = 0; i < 500 && job->state.load() != Job::Done; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(job->state.load() == Job::Done && job->result && job->result->size == ktx_size(EtcFormat::Rgb8, layout));
    // the cache file is written before the job reports Done
    auto hit = service.lookup(key, layout);
    CHECK(hit && hit->size == job->result->size &&
          memcmp(hit->data.get(), job->result->data.get(), hit->size) == 0);
    // a cancelled job is skipped
    auto cancelled_payload = payload;
    cancelled_payload[0] ^= 1;
    service.shutdown();
    service.configure(config, nullptr);
    // shared by two holders: one release keeps it, the last one cancels it
    auto job2 = service.submit(layout, key ^ 1, EtcFormat::Rgb8, cancelled_payload);
    auto job2_shared = service.submit(layout, key ^ 1, EtcFormat::Rgb8, cancelled_payload);
    CHECK(job2_shared == job2);
    auto observer = job2;
    service.release(job2_shared);
    CHECK(!job2_shared && !observer->cancelled);
    service.release(job2);
    CHECK(observer->cancelled);
    // a cancelled job is never handed out again
    auto job3 = service.submit(layout, key ^ 1, EtcFormat::Rgb8, cancelled_payload);
    CHECK(job3 && job3 != observer && !job3->cancelled);
    service.release(job3);
    service.shutdown();
    CHECK(observer->state.load() == Job::Failed && !observer->result);
}
} // namespace

int main()
{
    const fs::path directory = fs::temp_directory_path() / ("storm_texture_tests_" + std::to_string(getpid()));
    fs::create_directories(directory);
    test_decoder();
    test_channel_order();
    test_hash();
    test_quality();
    test_cube();
    test_cache_files(directory / "files");
    test_service(directory / "service");
    fs::remove_all(directory);
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("all texture tests passed\n");
    return failures ? 1 : 0;
}
