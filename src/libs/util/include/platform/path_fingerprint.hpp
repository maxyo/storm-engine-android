#pragma once

// Content-change fingerprint of a directory tree, used to invalidate the
// compiled script cache.
//
// The previous fingerprint was the plain sum of all modification times. It
// missed renames, files swapped between two trees with the same timestamps,
// edits that preserve the timestamp (archive extraction, file managers) and it
// was 0 for a directory spelled with different case than on disk. Each file now
// contributes a hash of (relative path, size, mtime); contributions are summed
// so the result does not depend on directory enumeration order.
//
// This is change detection for a cache, not a security boundary: file contents
// are not read, so an edit that keeps both size and mtime is not seen.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace storm::fingerprint
{
inline constexpr uint64_t kOffset = 14695981039346656037ull;
inline constexpr uint64_t kPrime = 1099511628211ull;

inline uint64_t fnv1a(std::string_view text, uint64_t hash = kOffset)
{
    for (const unsigned char c : text)
    {
        hash ^= c;
        hash *= kPrime;
    }
    return hash;
}

inline uint64_t fnv1a(uint64_t value, uint64_t hash)
{
    for (int i = 0; i < 8; ++i)
    {
        hash ^= (value >> (i * 8)) & 0xff;
        hash *= kPrime;
    }
    return hash;
}

// Finalizer (splitmix64) so that summing per-file hashes keeps them independent.
inline uint64_t mix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// 0 for a missing tree. Unreadable entries are skipped rather than thrown:
// a cache key must never abort startup.
inline uint64_t tree(const std::filesystem::path &root)
{
    namespace fs = std::filesystem;
    std::error_code error;
    uint64_t result = 0;
    const auto one = [&](const fs::path &file, const std::string &name) {
        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        if (ec)
            return;
        const auto time = fs::last_write_time(file, ec);
        if (ec)
            return;
        uint64_t hash = fnv1a(name);
        hash = fnv1a(static_cast<uint64_t>(size), hash);
        hash = fnv1a(static_cast<uint64_t>(time.time_since_epoch().count()), hash);
        result += mix(hash);
    };
    if (fs::is_regular_file(root, error))
    {
        one(root, root.filename().generic_string());
        return result;
    }
    if (!fs::is_directory(root, error))
        return 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
    {
        std::error_code ec;
        if (it->is_regular_file(ec) && !ec)
            one(it->path(), it->path().lexically_relative(root).generic_string());
    }
    return result;
}
} // namespace storm::fingerprint
