#include "storm/fx/relief_store.hpp"

#include "storm/fx/gloss_map.hpp"
#include "storm/fx/relief_model.hpp"
#include "storm/fx/height_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <system_error>
#include <vector>

namespace storm::fx
{
namespace
{
constexpr char kMagic[8] = {'S', 'T', 'R', 'E', 'L', 'I', 'E', 'F'};
constexpr uint32_t kFileVersion = 4;

struct Header
{
    char magic[8];
    uint32_t version, width, height, levels, format;
    uint64_t payload;        // the normals (NormalMap::rg)
    uint64_t material_payload; // the material texture after them (NormalMap::material): the normals' bytes, or 0
    float depth;             // NormalMap::depth
    uint32_t trust_bytes;    // the trust mask after the material (NormalMap::trust)
    float trust_coverage;    // NormalMap::trust_coverage
    uint32_t reserved;
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
    // The first level no larger than kReliefProbeSize; a texture whose smallest level is larger (few mips: the decks'
    // 512 textures have three) is probed at that smallest level. Skipped top mips (texture_degradation) never change it.
    for (int level = 0; level < levels; ++level)
    {
        if (width < 4 || height < 4)
            return -1;
        if (std::max(width, height) <= kReliefProbeSize || level + 1 == levels)
            return level;
        width /= 2;
        height /= 2;
    }
    return -1;
}

uint64_t ReliefMethod(ReliefClass relief)
{
    if (relief == ReliefClass::None)
        return 0;
    uint64_t method = 0xcbf29ce484222325ull;
    if (relief == ReliefClass::HeightMap)
    {
        NormalMapOptions options = HeightMapOptions();
        options.max_size = kReliefMapSize; // as the game derives them (effects/normal_maps.cpp)
        const uint64_t parts[3] = {kReliefHeightMethod, NormalMapCacheKey(options), kHeightMapVersion}; // with their heights
        return Mix(method, parts, sizeof(parts));
    }
    // PBRify's normals, height and roughness (their models' digests), the material's height and gloss derivations.
    const uint64_t parts[7] = {kReliefMethodVersion, kReliefNormalModelDigest, kReliefHeightModelDigest,
                               kReliefRoughnessModelDigest, static_cast<uint64_t>(kReliefMapSize),
                               ReliefHasHeight(relief) ? kHeightMapVersion : 0u, kGlossMethod};
    return Mix(method, parts, sizeof(parts));
}

uint64_t ReliefKey(std::string_view normalized_path, std::span<const uint8_t> probe_level, int probe_width,
                   int probe_height, uint64_t method, int map_size)
{
    uint64_t key = 0xcbf29ce484222325ull;
    const uint64_t parts[4] = {method, static_cast<uint64_t>(probe_width), static_cast<uint64_t>(probe_height),
                               static_cast<uint64_t>(map_size)};
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

std::filesystem::path GlossFile(const std::filesystem::path &directory, uint64_t key)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.gloss", static_cast<unsigned long long>(key));
    return directory / name;
}

namespace
{
bool WriteMapFile(const std::filesystem::path &target, const NormalMap &map)
{
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
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
        header.material_payload = map.material.size();
        header.depth = map.depth;
        header.trust_bytes = static_cast<uint32_t>(map.trust.size());
        header.trust_coverage = map.trust_coverage;
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        out.write(reinterpret_cast<const char *>(map.rg.data()), static_cast<std::streamsize>(map.rg.size()));
        out.write(reinterpret_cast<const char *>(map.material.data()), static_cast<std::streamsize>(map.material.size()));
        out.write(reinterpret_cast<const char *>(map.trust.data()), static_cast<std::streamsize>(map.trust.size()));
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

// `formats`: the formats a file of this kind may hold.
std::optional<NormalMap> ReadMapFile(const std::filesystem::path &path, std::initializer_list<NormalMapFormat> formats,
                                     std::string *error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    Header header{};
    in.read(reinterpret_cast<char *>(&header), sizeof(header));
    NormalMap map;
    bool ok = in.good() && std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 && header.version == kFileVersion &&
              header.width > 0 && header.height > 0 && header.width <= 16384 && header.height <= 16384 &&
              std::find(formats.begin(), formats.end(), static_cast<NormalMapFormat>(header.format)) != formats.end();
    if (ok)
    {
        map.width = static_cast<int>(header.width);
        map.height = static_cast<int>(header.height);
        map.mip_count = static_cast<int>(header.levels);
        map.format = static_cast<NormalMapFormat>(header.format);
        int levels = 1;
        for (int w = map.width, h = map.height; w > 1 || h > 1; w = std::max(w / 2, 1), h = std::max(h / 2, 1))
            ++levels;
        ok = map.mip_count == levels && header.payload == NormalMapBytes(map.width, map.height, map.format) &&
             (header.material_payload == 0 || header.material_payload == header.payload) &&
             std::isfinite(header.depth) && header.depth >= 0.0f &&
             std::isfinite(header.trust_coverage) && header.trust_coverage >= 0.0f && header.trust_coverage <= 1.0f &&
             header.trust_bytes <= header.payload;
    }
    if (ok)
    {
        map.rg.resize(header.payload);
        in.read(reinterpret_cast<char *>(map.rg.data()), static_cast<std::streamsize>(map.rg.size()));
        ok = in.gcount() == static_cast<std::streamsize>(map.rg.size());
        map.material.resize(header.material_payload);
        map.depth = header.material_payload ? header.depth : 0.0f;
        if (ok && !map.material.empty())
        {
            in.read(reinterpret_cast<char *>(map.material.data()), static_cast<std::streamsize>(map.material.size()));
            ok = in.gcount() == static_cast<std::streamsize>(map.material.size());
        }
        map.trust.resize(header.trust_bytes);
        map.trust_coverage = header.trust_coverage;
        if (ok && !map.trust.empty())
        {
            in.read(reinterpret_cast<char *>(map.trust.data()), static_cast<std::streamsize>(map.trust.size()));
            ok = in.gcount() == static_cast<std::streamsize>(map.trust.size());
        }
    }
    if (!ok)
    {
        in.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        if (error)
            *error = "damaged or outdated map file " + path.string() + " (deleted)";
        return std::nullopt;
    }
    return map;
}

bool IsFile(const std::filesystem::path &path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}
} // namespace

bool WriteReliefMap(const std::filesystem::path &directory, uint64_t key, const NormalMap &map)
{
    return WriteMapFile(ReliefFile(directory, key), map);
}

std::optional<NormalMap> ReadReliefMap(const std::filesystem::path &directory, uint64_t key, std::string *error)
{
    return ReadMapFile(ReliefFile(directory, key), {NormalMapFormat::Rg8, NormalMapFormat::EacRg11}, error);
}

bool HasReliefMap(const std::filesystem::path &directory, uint64_t key)
{
    return IsFile(ReliefFile(directory, key));
}

bool WriteGlossMap(const std::filesystem::path &directory, uint64_t key, const NormalMap &map)
{
    return WriteMapFile(GlossFile(directory, key), map);
}

std::optional<NormalMap> ReadGlossMap(const std::filesystem::path &directory, uint64_t key, std::string *error)
{
    return ReadMapFile(GlossFile(directory, key), {NormalMapFormat::EacRg11}, error);
}

bool HasGlossMap(const std::filesystem::path &directory, uint64_t key)
{
    return IsFile(GlossFile(directory, key));
}

} // namespace storm::fx
