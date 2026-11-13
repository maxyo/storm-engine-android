#include "storm/fx/upscale.hpp"

#include "storm/fx/relief_store.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <system_error>

namespace storm::fx
{
namespace
{
constexpr char kMagic[8] = {'S', 'T', 'U', 'P', 'X', '2', 0, 0};
constexpr uint32_t kFileVersion = 1;
constexpr const char *kExtension = ".x2";

struct Header
{
    char magic[8];
    uint32_t version, reserved;
    uint64_t key, payload, checksum;
};
static_assert(sizeof(Header) == 40);

uint64_t Mix(uint64_t hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    return hash;
}

// FNV-1a over 8-byte words (the containers are megabytes: the byte loop above costs ~1 ms per MB).
uint64_t Checksum(std::span<const uint8_t> data, uint64_t key)
{
    uint64_t hash = 0xcbf29ce484222325ull ^ key;
    size_t i = 0;
    for (; i + 8 <= data.size(); i += 8)
    {
        uint64_t word;
        std::memcpy(&word, data.data() + i, 8);
        hash = (hash ^ word) * 0x100000001b3ull;
    }
    return Mix(hash, data.data() + i, data.size() - i);
}
} // namespace

const char *UpscaleTargetName(UpscaleTarget target)
{
    switch (target)
    {
    case UpscaleTarget::Surface:
        return "surface";
    case UpscaleTarget::Character:
        return "character";
    default:
        return "none";
    }
}

UpscaleTarget ClassifyUpscale(std::string_view normalized_path, ReliefClass relief, bool cutout)
{
    if (cutout || relief == ReliefClass::HeightMap)
        return UpscaleTarget::None;
    if (normalized_path.starts_with("characters/"))
        return UpscaleTarget::Character;
    return relief == ReliefClass::None ? UpscaleTarget::None : UpscaleTarget::Surface;
}

bool UpscaleWraps(UpscaleTarget target, ReliefClass relief)
{
    return target == UpscaleTarget::Surface &&
           (relief == ReliefClass::Stone || relief == ReliefClass::Wood || relief == ReliefClass::Plaster);
}

bool UpscaleSize(int width, int height)
{
    return width > 0 && height > 0 && std::max(width, height) <= kUpscaleMaxSource;
}

bool ReliefMapsFromUpscale(int width, int height)
{
    return std::max(width, height) <= kReliefMapSize;
}

uint64_t UpscaleMethod(bool bc)
{
    const uint64_t parts[3] = {kUpscaleVersion, kUpscaleModelDigest, bc ? 1u : 0u};
    return Mix(0xcbf29ce484222325ull, parts, sizeof(parts));
}

std::filesystem::path UpscaleFile(const std::filesystem::path &directory, uint64_t key)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx%s", static_cast<unsigned long long>(key), kExtension);
    return directory / name;
}

bool WriteUpscaled(const std::filesystem::path &directory, uint64_t key, std::span<const uint8_t> ktx)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path file = UpscaleFile(directory, key);
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kFileVersion;
    header.key = key;
    header.payload = ktx.size();
    header.checksum = Checksum(ktx, key);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.write(reinterpret_cast<const char *>(&header), sizeof(header)) ||
            !out.write(reinterpret_cast<const char *>(ktx.data()), static_cast<std::streamsize>(ktx.size())))
        {
            out.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error)
        std::filesystem::remove(temporary, error);
    return !error;
}

std::optional<std::vector<uint8_t>> ReadUpscaled(const std::filesystem::path &directory, uint64_t key, std::string *error)
{
    const std::filesystem::path file = UpscaleFile(directory, key);
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    Header header{};
    std::vector<uint8_t> ktx;
    const char *problem = nullptr;
    if (!in.read(reinterpret_cast<char *>(&header), sizeof(header)) || std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
        header.version != kFileVersion || header.key != key || header.payload == 0 || header.payload > (256ull << 20))
        problem = "a damaged header or another layout";
    else
    {
        ktx.resize(static_cast<size_t>(header.payload));
        if (!in.read(reinterpret_cast<char *>(ktx.data()), static_cast<std::streamsize>(ktx.size())) || in.peek() != EOF)
            problem = "a wrong size";
        else if (Checksum(ktx, key) != header.checksum)
            problem = "a wrong checksum";
    }
    in.close();
    std::error_code ignored;
    if (problem)
    {
        std::filesystem::remove(file, ignored);
        if (error)
            *error = file.string() + " had " + problem + ": deleted";
        return std::nullopt;
    }
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ignored);
    return ktx;
}

bool HasUpscaled(const std::filesystem::path &directory, uint64_t key)
{
    std::error_code error;
    return std::filesystem::is_regular_file(UpscaleFile(directory, key), error);
}

uint64_t TrimUpscaled(const std::filesystem::path &directory, uint64_t max_bytes)
{
    struct File
    {
        std::filesystem::path path;
        std::filesystem::file_time_type time;
        uint64_t bytes;
    };
    std::vector<File> files;
    uint64_t total = 0;
    std::error_code error;
    for (auto it = std::filesystem::directory_iterator(directory, error); !error && it != std::filesystem::directory_iterator();
         it.increment(error))
    {
        std::error_code e;
        if (!it->is_regular_file(e))
            continue;
        const std::filesystem::path &path = it->path();
        if (path.extension() == ".tmp")
        {
            std::filesystem::remove(path, e); // left by a write that was cut off
            continue;
        }
        if (path.extension() != kExtension)
            continue;
        const uint64_t bytes = it->file_size(e);
        files.push_back({path, it->last_write_time(e), bytes});
        total += bytes;
    }
    if (total <= max_bytes)
        return total;
    std::sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.time < b.time; });
    for (const File &file : files)
    {
        if (total <= max_bytes)
            break;
        std::error_code e;
        if (std::filesystem::remove(file.path, e))
            total -= file.bytes;
    }
    return total;
}

uint64_t UpscaleBudgetBytes(uint64_t device_ram_bytes)
{
    return std::clamp<uint64_t>(device_ram_bytes / 32, 64ull << 20, 384ull << 20);
}

UpscalePlan PlanUpscaleResidency(std::span<const UpscaleCandidate> candidates, uint64_t budget_bytes)
{
    UpscalePlan plan;
    plan.keep.assign(candidates.size(), false);
    std::vector<size_t> order;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const UpscaleCandidate &c = candidates[i];
        if (c.distance >= 0.0f && c.distance <= (c.resident ? kUpscaleFar : kUpscaleNear))
            order.push_back(i);
    }
    // Nearest first; at the same distance the resident one (no swap for nothing).
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (candidates[a].distance != candidates[b].distance)
            return candidates[a].distance < candidates[b].distance;
        return candidates[a].resident > candidates[b].resident;
    });
    for (const size_t i : order)
    {
        if (plan.bytes + candidates[i].bytes <= budget_bytes)
        {
            plan.keep[i] = true;
            plan.bytes += candidates[i].bytes;
        }
        else if (candidates[i].distance <= kUpscaleNear)
        {
            ++plan.limited;
            if (plan.nearest_limited < 0.0f)
                plan.nearest_limited = candidates[i].distance;
        }
    }
    return plan;
}

} // namespace storm::fx
