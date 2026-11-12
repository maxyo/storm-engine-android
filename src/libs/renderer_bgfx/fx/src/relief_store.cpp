#include "storm/fx/relief_store.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

namespace storm::fx
{
namespace
{
constexpr char kMagic[8] = {'S', 'T', 'R', 'E', 'L', 'I', 'E', 'F'};
constexpr uint32_t kFileVersion = 1;

struct Header
{
    char magic[8];
    uint32_t version, width, height, levels, format;
    uint64_t payload;
};

uint64_t Mix(uint64_t key, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        key = (key ^ bytes[i]) * 0x100000001b3ull;
    return key;
}
} // namespace

int ReliefProbeLevel(int width, int height, int levels)
{
    for (int level = 0; level < levels; ++level)
    {
        if (width < 4 || height < 4)
            return -1;
        if (std::max(width, height) <= kReliefProbeSize)
            return level;
        width /= 2;
        height /= 2;
    }
    return -1;
}

uint64_t ReliefKey(std::string_view normalized_path, std::span<const uint8_t> probe_level, int probe_width,
                   int probe_height, uint32_t method, int map_size)
{
    uint64_t key = 0xcbf29ce484222325ull;
    const uint32_t parts[4] = {method, static_cast<uint32_t>(probe_width), static_cast<uint32_t>(probe_height),
                               static_cast<uint32_t>(map_size)};
    key = Mix(key, parts, sizeof(parts));
    key = Mix(key, normalized_path.data(), normalized_path.size());
    return Mix(key, probe_level.data(), probe_level.size());
}

std::filesystem::path ReliefFile(const std::filesystem::path &directory, uint64_t key)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.relief", static_cast<unsigned long long>(key));
    return directory / name;
}

bool WriteReliefMap(const std::filesystem::path &directory, uint64_t key, const NormalMap &map)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path target = ReliefFile(directory, key);
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        Header header{};
        std::memcpy(header.magic, kMagic, sizeof(kMagic));
        header.version = kFileVersion;
        header.width = static_cast<uint32_t>(map.width);
        header.height = static_cast<uint32_t>(map.height);
        header.levels = static_cast<uint32_t>(map.mip_count);
        header.format = static_cast<uint32_t>(map.format);
        header.payload = map.rg.size();
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        out.write(reinterpret_cast<const char *>(map.rg.data()), static_cast<std::streamsize>(map.rg.size()));
        if (!out.good())
        {
            out.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, target, error);
    if (error)
        std::filesystem::remove(temporary, error);
    return !error;
}

std::optional<NormalMap> ReadReliefMap(const std::filesystem::path &directory, uint64_t key, std::string *error)
{
    const std::filesystem::path path = ReliefFile(directory, key);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    Header header{};
    in.read(reinterpret_cast<char *>(&header), sizeof(header));
    NormalMap map;
    bool ok = in.good() && std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 && header.version == kFileVersion &&
              header.width > 0 && header.height > 0 && header.width <= 16384 && header.height <= 16384 &&
              header.format <= static_cast<uint32_t>(NormalMapFormat::EacRg11);
    if (ok)
    {
        map.width = static_cast<int>(header.width);
        map.height = static_cast<int>(header.height);
        map.mip_count = static_cast<int>(header.levels);
        map.format = static_cast<NormalMapFormat>(header.format);
        int levels = 1;
        for (int w = map.width, h = map.height; w > 1 || h > 1; w = std::max(w / 2, 1), h = std::max(h / 2, 1))
            ++levels;
        ok = map.mip_count == levels && header.payload == NormalMapBytes(map.width, map.height, map.format);
    }
    if (ok)
    {
        map.rg.resize(header.payload);
        in.read(reinterpret_cast<char *>(map.rg.data()), static_cast<std::streamsize>(map.rg.size()));
        ok = in.gcount() == static_cast<std::streamsize>(map.rg.size());
    }
    if (!ok)
    {
        in.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        if (error)
            *error = "damaged or outdated relief file " + path.string() + " (deleted)";
        return std::nullopt;
    }
    return map;
}

bool HasReliefMap(const std::filesystem::path &directory, uint64_t key)
{
    std::error_code error;
    return std::filesystem::is_regular_file(ReliefFile(directory, key), error);
}

} // namespace storm::fx
