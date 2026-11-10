#include "cache.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace storm::texture
{
namespace fs = std::filesystem;
namespace
{
constexpr char kMagic[8] = {'S', 'T', 'X', 'C', 'E', 'T', 'C', '2'};
constexpr const char *kExtension = ".etc2";
constexpr uint32_t kKtxHeaderSize = 64;

constexpr uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull, P3 = 1609587929392839161ull,
                   P4 = 9650029242287828579ull, P5 = 2870177450012600261ull;

uint64_t rotl(uint64_t x, int r) { return x << r | x >> (64 - r); }
uint64_t read64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
uint32_t read32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
uint64_t round64(uint64_t acc, uint64_t input) { return rotl(acc + input * P2, 31) * P1; }
uint64_t merge64(uint64_t acc, uint64_t value) { return (acc ^ round64(0, value)) * P1 + P4; }

void put32(uint8_t *&out, uint32_t value)
{
    memcpy(out, &value, 4);
    out += 4;
}
} // namespace

// xxHash64 (little-endian hosts only, as every target of the engine).
uint64_t hash64(const void *data, size_t size, uint64_t seed)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    const uint8_t *end = p + size;
    uint64_t h;
    if (size >= 32)
    {
        uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        for (; p + 32 <= end; p += 32)
        {
            v1 = round64(v1, read64(p));
            v2 = round64(v2, read64(p + 8));
            v3 = round64(v3, read64(p + 16));
            v4 = round64(v4, read64(p + 24));
        }
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        h = merge64(merge64(merge64(merge64(h, v1), v2), v3), v4);
    }
    else
        h = seed + P5;
    h += size;
    for (; p + 8 <= end; p += 8)
        h = rotl(h ^ round64(0, read64(p)), 27) * P1 + P4;
    if (p + 4 <= end)
    {
        h = rotl(h ^ (uint64_t(read32(p)) * P1), 23) * P2 + P3;
        p += 4;
    }
    for (; p < end; ++p)
        h = rotl(h ^ (*p * P5), 11) * P1;
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}

KeyBuilder::KeyBuilder(const Layout &layout)
{
    const uint32_t shape[6] = {kEncoderVersion, uint32_t(layout.format), layout.width, layout.height, layout.levels,
                               layout.faces};
    state_ = hash64(shape, sizeof(shape), 0x53544f524d544558ull);
}

void KeyBuilder::add(const uint8_t *data, size_t size) { state_ = hash64(data, size, state_); }

size_t ktx_size(EtcFormat format, const Layout &layout)
{
    size_t total = kKtxHeaderSize;
    for (uint32_t level = 0; level < layout.levels; ++level)
        total += 4 + etc_level_size(format, layout.level_width(level), layout.level_height(level)) * layout.faces;
    return total;
}

Blob transcode_to_ktx(const Layout &layout, const uint8_t *payload, EtcFormat format)
{
    Blob blob;
    blob.size = ktx_size(format, layout);
    blob.data = std::make_unique<uint8_t[]>(blob.size);
    uint8_t *out = blob.data.get();
    static constexpr uint8_t kIdentifier[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n'};
    memcpy(out, kIdentifier, 12);
    out += 12;
    put32(out, 0x04030201);
    put32(out, 0); // glType
    put32(out, 1); // glTypeSize
    put32(out, 0); // glFormat
    put32(out, format == EtcFormat::Rgb8 ? 0x9274 : 0x9278); // COMPRESSED_RGB8_ETC2 / RGBA8_ETC2_EAC
    put32(out, format == EtcFormat::Rgb8 ? 0x1907 : 0x1908); // GL_RGB / GL_RGBA
    put32(out, layout.width);
    put32(out, layout.height);
    put32(out, 0); // depth
    put32(out, 0); // array elements
    put32(out, layout.faces);
    put32(out, layout.levels);
    put32(out, 0); // key/value data

    // offsets of each (face, level) inside the face-major D3D payload
    std::vector<size_t> source_offset(size_t(layout.faces) * layout.levels);
    size_t offset = 0;
    for (uint32_t face = 0; face < layout.faces; ++face)
        for (uint32_t level = 0; level < layout.levels; ++level)
        {
            source_offset[face * layout.levels + level] = offset;
            offset += dxt_level_size(layout.format, layout.level_width(level), layout.level_height(level));
        }

    std::vector<uint8_t> rgba;
    EncodeScratch scratch;
    for (uint32_t level = 0; level < layout.levels; ++level)
    {
        const uint32_t w = layout.level_width(level), h = layout.level_height(level);
        const uint32_t aw = blocks_x(w) * 4, ah = blocks_x(h) * 4;
        const size_t face_bytes = etc_level_size(format, w, h);
        // KTX: imageSize is the size of one face for non-array cube maps
        put32(out, uint32_t(face_bytes));
        rgba.resize(size_t(aw) * ah * 4);
        for (uint32_t face = 0; face < layout.faces; ++face)
        {
            decode_level(layout.format, payload + source_offset[face * layout.levels + level], w, h, rgba.data());
            encode_level(format, rgba.data(), aw, ah, out, scratch);
            out += face_bytes;
        }
    }
    return blob;
}

fs::path cache_file_path(const fs::path &directory, uint64_t key)
{
    char name[32];
    snprintf(name, sizeof(name), "%016llx%s", static_cast<unsigned long long>(key), kExtension);
    return directory / name;
}

FileHeader make_header(uint64_t key, const Layout &layout, EtcFormat format, const Blob &ktx)
{
    FileHeader header{};
    memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kEncoderVersion;
    header.etc_format = uint32_t(format);
    header.width = layout.width;
    header.height = layout.height;
    header.levels = layout.levels;
    header.faces = layout.faces;
    header.key = key;
    header.payload_size = ktx.size;
    header.checksum = hash64(ktx.data.get(), ktx.size, key);
    return header;
}

bool header_valid(const FileHeader &header, uint64_t key, const Layout &layout)
{
    if (memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kEncoderVersion ||
        header.key != key || header.width != layout.width || header.height != layout.height ||
        header.levels != layout.levels || header.faces != layout.faces)
        return false;
    if (header.etc_format != uint32_t(EtcFormat::Rgb8) && header.etc_format != uint32_t(EtcFormat::Rgba8))
        return false;
    return header.payload_size == ktx_size(EtcFormat(header.etc_format), layout);
}

uint64_t write_cache_file(const fs::path &directory, uint64_t key, const Layout &layout, EtcFormat format,
                          const Blob &ktx)
{
    std::error_code ec;
    fs::create_directories(directory, ec);
    const fs::path target = cache_file_path(directory, key);
    fs::path temp = target;
    temp += "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffff) + ".tmp";
    const FileHeader header = make_header(key, layout, format, ktx);
    FILE *file = fopen(temp.c_str(), "wb");
    if (!file)
        return 0;
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 && fwrite(ktx.data.get(), 1, ktx.size, file) == ktx.size;
    ok = fclose(file) == 0 && ok;
    if (ok)
        fs::rename(temp, target, ec);
    if (!ok || ec)
    {
        fs::remove(temp, ec);
        return 0;
    }
    return sizeof(header) + ktx.size;
}

std::optional<Blob> read_cache_file(const fs::path &directory, uint64_t key, const Layout &layout)
{
    const fs::path path = cache_file_path(directory, key);
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
        return std::nullopt;
    FileHeader header{};
    Blob blob;
    bool ok = fread(&header, sizeof(header), 1, file) == 1 && header_valid(header, key, layout);
    if (ok)
    {
        blob.size = header.payload_size;
        blob.data = std::make_unique<uint8_t[]>(blob.size);
        ok = fread(blob.data.get(), 1, blob.size, file) == blob.size && fgetc(file) == EOF &&
             hash64(blob.data.get(), blob.size, key) == header.checksum;
    }
    fclose(file);
    std::error_code ec;
    if (!ok)
    {
        fs::remove(path, ec);
        return std::nullopt;
    }
    fs::last_write_time(path, fs::file_time_type::clock::now(), ec); // LRU stamp
    return blob;
}

uint64_t scan_cache(const fs::path &directory)
{
    uint64_t total = 0;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
        const fs::path &path = it->path();
        if (path.extension() == ".tmp")
            fs::remove(path, ec);
        else if (path.extension() == kExtension)
            total += it->file_size(ec);
        ec.clear();
    }
    return total;
}

uint64_t evict_cache(const fs::path &directory, uint64_t target_bytes)
{
    struct Entry
    {
        fs::file_time_type time;
        uint64_t size;
        fs::path path;
    };
    std::vector<Entry> entries;
    uint64_t total = 0;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->path().extension() != kExtension)
            continue;
        Entry entry{it->last_write_time(ec), it->file_size(ec), it->path()};
        if (ec)
        {
            ec.clear();
            continue;
        }
        total += entry.size;
        entries.push_back(std::move(entry));
    }
    if (total <= target_bytes)
        return total;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.time < b.time; });
    for (const Entry &entry : entries)
    {
        if (total <= target_bytes)
            break;
        if (fs::remove(entry.path, ec))
            total -= entry.size;
        ec.clear();
    }
    return total;
}

} // namespace storm::texture
