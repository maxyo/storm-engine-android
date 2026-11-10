#pragma once

// On-disk layout of the compiled script cache (COMPILER, `[script] cache_mode`).
//
//   <cache folder>/state        8-byte key of everything the bytecode depends on
//   <cache folder>/<segment>.b  header (magic, payload size, payload FNV-1a) + payload
//
// The compiler compares the stored key with the current one before using any
// entry; on a mismatch, a missing or a damaged state file it empties the
// folder (resetFolder) and compiles from source. Every outcome is reported to
// the caller so it can be logged: nothing here falls back silently.

#include "platform/path_fingerprint.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace storm::script_cache
{
inline constexpr char kStateFile[] = "state";
inline constexpr char kEntryMagic[8] = {'S', 'T', 'O', 'R', 'M', 'S', 'C', '2'};

struct EntryHeader
{
    char magic[8];
    uint64_t payload_size;
    uint64_t payload_hash;
};

enum class StateFile
{
    Missing, // first start, or the folder was cleared
    Damaged, // present but too short to hold a key
    Read,
};

inline StateFile readState(const std::filesystem::path &folder, uint64_t &key)
{
    std::ifstream stream(folder / kStateFile, std::ios::binary);
    if (!stream)
    {
        return StateFile::Missing;
    }
    uint64_t stored{};
    if (!stream.read(reinterpret_cast<char *>(&stored), sizeof(stored)))
    {
        return StateFile::Damaged;
    }
    key = stored;
    return StateFile::Read;
}

// Empties the folder and stamps it with the key. Returns an empty string on
// success, otherwise what failed.
inline std::string resetFolder(const std::filesystem::path &folder, uint64_t key)
{
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    if (!ec)
    {
        std::filesystem::create_directories(folder, ec);
    }
    if (ec)
    {
        return "cannot reset '" + folder.string() + "': " + ec.message();
    }
    std::ofstream stream(folder / kStateFile, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char *>(&key), sizeof(key));
    stream.close();
    if (!stream)
    {
        return "cannot write '" + (folder / kStateFile).string() + "'";
    }
    return {};
}

// Writes beside the target and renames, so a reader never sees a partial entry.
inline std::string writeEntry(const std::filesystem::path &path, std::string_view payload)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return "cannot create '" + path.parent_path().string() + "': " + ec.message();
    }
    EntryHeader header{};
    std::copy(std::begin(kEntryMagic), std::end(kEntryMagic), header.magic);
    header.payload_size = payload.size();
    header.payload_hash = fingerprint::fnv1a(payload);

    auto pending = path;
    pending += ".tmp";
    {
        std::ofstream stream(pending, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
        stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        stream.close();
        if (stream.fail())
        {
            std::filesystem::remove(pending, ec);
            return "cannot write '" + pending.string() + "'";
        }
    }
    std::filesystem::rename(pending, path, ec);
    if (ec)
    {
        std::filesystem::remove(pending, ec);
        return "cannot rename to '" + path.string() + "': " + ec.message();
    }
    return {};
}

enum class Entry
{
    Missing, // not cached yet: an ordinary miss
    Damaged, // torn, truncated or bit-rotted: the caller deletes it and recompiles
    Read,
};

inline Entry readEntry(const std::filesystem::path &path, std::vector<char> &payload)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
    {
        return Entry::Missing;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size <= sizeof(EntryHeader))
    {
        return Entry::Damaged;
    }
    std::ifstream stream(path, std::ios::binary);
    EntryHeader header{};
    std::vector<char> data(size - sizeof(header));
    if (!stream.read(reinterpret_cast<char *>(&header), sizeof(header)) ||
        !stream.read(data.data(), static_cast<std::streamsize>(data.size())))
    {
        return Entry::Damaged;
    }
    if (!std::equal(std::begin(kEntryMagic), std::end(kEntryMagic), header.magic) ||
        header.payload_size != data.size() ||
        header.payload_hash != fingerprint::fnv1a(std::string_view(data.data(), data.size())))
    {
        return Entry::Damaged;
    }
    payload = std::move(data);
    return Entry::Read;
}
} // namespace storm::script_cache
