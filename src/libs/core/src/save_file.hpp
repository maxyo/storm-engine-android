#pragma once

// Save file container and crash-safe replacement.
//
// On-disk layout (unchanged from the original engine, so old saves load and
// new saves load in old builds):
//
//   Header                     sFileInfo[32], extOffset, extSize
//   uint32 rawSize             size of the uncompressed script state
//   uint32 packedSize
//   byte   packed[packedSize]  zlib stream (any level)
//   --- optional, at extOffset ---
//   uint32 extPackedSize
//   byte   extPacked[...]      zlib stream of extSize bytes (description + thumbnail)
//
// zlib streams end with an Adler-32 of the uncompressed data, so a stream that
// inflates with Z_OK to the declared size is intact. Nothing here trusts a
// size field before checking it against the file.
//
// writeAtomic() never exposes a partially written save under the target name:
// data goes to "<name>.storm-tmp", is fsync'ed and read back, the previous
// save is kept as "<name>.storm-bak" until the new file is in place and the
// directory entry is durable, and only then the backup is removed.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include <zlib.h>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace storm::save
{
namespace fs = std::filesystem;

// Level 9 cost ~165 ms for a 5 MiB state on a desktop core; level 4 costs
// ~23 ms for a file about 16% larger. Loading accepts every level.
inline constexpr int kCompressionLevel = 4;
inline constexpr uint32_t kMaxSectionSize = 0x8000000; // limit inherited from the original loader
inline constexpr char kTempSuffix[] = ".storm-tmp";
inline constexpr char kBackupSuffix[] = ".storm-bak";

struct Header
{
    char sFileInfo[32];
    uint32_t dwExtDataOffset;
    uint32_t dwExtDataSize;
};
static_assert(sizeof(Header) == 40, "save header layout is part of the file format");

using Bytes = std::vector<char>;

inline bool isAuxiliaryName(const std::string &name)
{
    const auto ends = [&](const char *suffix) {
        const auto n = std::strlen(suffix);
        return name.size() >= n && name.compare(name.size() - n, n, suffix) == 0;
    };
    return ends(kTempSuffix) || ends(kBackupSuffix);
}

inline fs::path withSuffix(const fs::path &path, const char *suffix)
{
    auto result = path;
    result += suffix;
    return result;
}

inline Header makeHeader(const char *info)
{
    Header header{};
    std::snprintf(header.sFileInfo, sizeof(header.sFileInfo), "%s", info && info[0] ? info : "save");
    return header;
}

inline bool pack(const void *data, uint32_t size, Bytes &out, int level = kCompressionLevel)
{
    uLongf packed = compressBound(size);
    const auto offset = out.size();
    out.resize(offset + packed);
    if (compress2(reinterpret_cast<Bytef *>(out.data() + offset), &packed, static_cast<const Bytef *>(data), size,
                  level) != Z_OK)
    {
        out.resize(offset);
        return false;
    }
    out.resize(offset + packed);
    return true;
}

inline void append(Bytes &out, const void *data, size_t size)
{
    const auto *bytes = static_cast<const char *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

inline void patch(Bytes &out, size_t offset, uint32_t value)
{
    std::memcpy(out.data() + offset, &value, sizeof(value));
}

// Complete file image for a script state without extended data.
inline bool buildState(const char *info, const void *raw, uint32_t rawSize, Bytes &image, std::string &error,
                       int level = kCompressionLevel)
{
    image.clear();
    if (raw == nullptr || rawSize == 0 || rawSize > kMaxSectionSize)
    {
        error = "empty or oversized script state";
        return false;
    }
    const Header header = makeHeader(info);
    append(image, &header, sizeof(header));
    append(image, &rawSize, sizeof(rawSize));
    const auto packedSizeAt = image.size();
    const uint32_t zero = 0;
    append(image, &zero, sizeof(zero));
    if (!pack(raw, rawSize, image, level))
    {
        error = "cannot compress script state";
        return false;
    }
    patch(image, packedSizeAt, static_cast<uint32_t>(image.size() - packedSizeAt - sizeof(uint32_t)));
    return true;
}

// Inflate a section and require a clean stream of at most `declared` bytes.
inline bool unpack(const char *packed, uint32_t packedSize, uint32_t declared, Bytes &out, std::string &error)
{
    if (packedSize == 0 || packedSize > kMaxSectionSize || declared == 0 || declared > kMaxSectionSize)
    {
        error = "implausible section size";
        return false;
    }
    out.resize(declared);
    uLongf size = declared;
    const int result = uncompress(reinterpret_cast<Bytef *>(out.data()), &size,
                                  reinterpret_cast<const Bytef *>(packed), packedSize);
    if (result != Z_OK)
    {
        out.clear();
        error = result == Z_BUF_ERROR ? "compressed data is truncated or larger than declared"
                                      : result == Z_DATA_ERROR ? "compressed data is corrupt" : "zlib failure";
        return false;
    }
    out.resize(size);
    return true;
}

// Extract and validate the script state of a file image.
inline bool readState(const Bytes &image, Bytes &raw, std::string &error)
{
    raw.clear();
    constexpr size_t fixed = sizeof(Header) + 2 * sizeof(uint32_t);
    if (image.size() < fixed)
    {
        error = "file is shorter than a save header";
        return false;
    }
    uint32_t rawSize, packedSize;
    std::memcpy(&rawSize, image.data() + sizeof(Header), sizeof(rawSize));
    std::memcpy(&packedSize, image.data() + sizeof(Header) + sizeof(uint32_t), sizeof(packedSize));
    if (packedSize > image.size() - fixed)
    {
        error = "file is truncated";
        return false;
    }
    return unpack(image.data() + fixed, packedSize, rawSize, raw, error);
}

// Extended data (may legitimately be absent: `data` stays empty and the call succeeds).
inline bool readExtData(const Bytes &image, Bytes &data, std::string &error)
{
    data.clear();
    if (image.size() < sizeof(Header) + sizeof(uint32_t))
    {
        error = "file is shorter than a save header";
        return false;
    }
    Header header;
    std::memcpy(&header, image.data(), sizeof(header));
    if (header.dwExtDataSize == 0)
        return true;
    if (header.dwExtDataOffset < sizeof(Header) || header.dwExtDataOffset > image.size() - sizeof(uint32_t))
    {
        error = "extended data offset is outside the file";
        return false;
    }
    uint32_t packedSize;
    std::memcpy(&packedSize, image.data() + header.dwExtDataOffset, sizeof(packedSize));
    const size_t start = header.dwExtDataOffset + sizeof(uint32_t);
    if (packedSize > image.size() - start)
    {
        error = "extended data is truncated";
        return false;
    }
    return unpack(image.data() + start, packedSize, header.dwExtDataSize, data, error);
}

// New image = old image + extended data appended at the end (the original
// SetSaveData semantics), header rewritten to point at it.
inline bool appendExtData(Bytes &image, const char *info, const void *data, uint32_t size, std::string &error,
                          int level = kCompressionLevel)
{
    if (image.size() < sizeof(Header) || image.size() > UINT32_MAX - sizeof(uint32_t))
    {
        error = "file is shorter than a save header";
        return false;
    }
    if (data == nullptr || size == 0 || size > kMaxSectionSize)
    {
        error = "empty or oversized extended data";
        return false;
    }
    Header header = makeHeader(info);
    header.dwExtDataOffset = static_cast<uint32_t>(image.size());
    header.dwExtDataSize = size;
    const auto packedSizeAt = image.size();
    const uint32_t zero = 0;
    append(image, &zero, sizeof(zero));
    if (!pack(data, size, image, level))
    {
        image.resize(packedSizeAt);
        error = "cannot compress extended data";
        return false;
    }
    patch(image, packedSizeAt, static_cast<uint32_t>(image.size() - packedSizeAt - sizeof(uint32_t)));
    std::memcpy(image.data(), &header, sizeof(header));
    return true;
}

inline bool readFile(const fs::path &path, Bytes &data, std::string &error)
{
    data.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "cannot open file";
        return false;
    }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size < 0 || static_cast<uint64_t>(size) > 2ull * kMaxSectionSize)
    {
        error = "implausible file size";
        return false;
    }
    data.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(data.data(), size))
    {
        data.clear();
        error = "cannot read file";
        return false;
    }
    return true;
}

namespace detail
{
inline std::string lastError(const char *what)
{
    return std::string(what) + ": " + std::generic_category().message(errno);
}

// Write and flush to stable storage. Returns false with `error` set.
inline bool writeDurable(const fs::path &path, const char *data, size_t size, std::string &error)
{
#ifdef _WIN32
    FILE *file = _wfopen(path.c_str(), L"wb");
    if (!file)
    {
        error = lastError("create temporary save");
        return false;
    }
    bool ok = std::fwrite(data, 1, size, file) == size && std::fflush(file) == 0 && _commit(_fileno(file)) == 0;
    if (!ok)
        error = lastError("write temporary save");
    if (std::fclose(file) != 0 && ok)
    {
        ok = false;
        error = lastError("close temporary save");
    }
    return ok;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        error = lastError("create temporary save");
        return false;
    }
    bool ok = true;
    size_t written = 0;
    while (written < size)
    {
        const auto n = ::write(fd, data + written, size - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            ok = false;
            error = lastError("write temporary save");
            break;
        }
        written += static_cast<size_t>(n);
    }
    if (ok && ::fsync(fd) != 0)
    {
        ok = false;
        error = lastError("sync temporary save");
    }
    if (::close(fd) != 0 && ok)
    {
        ok = false;
        error = lastError("close temporary save");
    }
    return ok;
#endif
}

// Make a rename durable. Some filesystems refuse directory fsync; that is not
// a reason to fail a save whose data is already synced.
inline void syncDirectory(const fs::path &directory)
{
#ifndef _WIN32
    const int fd = ::open(directory.empty() ? "." : directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0)
    {
        (void)::fsync(fd);
        ::close(fd);
    }
#else
    (void)directory;
#endif
}

inline void removeQuietly(const fs::path &path)
{
    std::error_code ignored;
    fs::remove(path, ignored);
}
} // namespace detail

// Test seam: lets a test stop the sequence after a named step to model a crash.
// Steps, in order: "temp-written", "backup-made", "renamed".
using CrashHook = std::function<bool(const char *step)>;

// Replace `target` with `image`. `validate` is run on the bytes read back from
// the temporary file; the target is only touched after it accepts them.
// On failure the previous target (if any) is left untouched and still loadable.
inline bool writeAtomic(const fs::path &target, const Bytes &image,
                        const std::function<bool(const Bytes &, std::string &)> &validate, std::string &error,
                        const CrashHook &crash = {})
{
    const auto temp = withSuffix(target, kTempSuffix);
    const auto backup = withSuffix(target, kBackupSuffix);
    const auto stop = [&](const char *step) { return crash && crash(step); };

    if (!detail::writeDurable(temp, image.data(), image.size(), error))
    {
        detail::removeQuietly(temp);
        return false;
    }

    // Verify what actually reached the file, not the buffer we meant to write.
    Bytes stored;
    if (!readFile(temp, stored, error) || stored.size() != image.size() ||
        std::memcmp(stored.data(), image.data(), image.size()) != 0)
    {
        if (error.empty())
            error = "temporary save differs from the written data";
        detail::removeQuietly(temp);
        return false;
    }
    if (validate && !validate(stored, error))
    {
        detail::removeQuietly(temp);
        return false;
    }
    if (stop("temp-written"))
        return false;

    // Keep the previous save reachable under the backup name without ever
    // removing the target name: hard link where supported, copy otherwise.
    std::error_code ec;
    const bool hadTarget = fs::is_regular_file(target, ec) && !ec;
    bool hasBackup = false;
    if (hadTarget)
    {
        detail::removeQuietly(backup);
        fs::create_hard_link(target, backup, ec);
        if (ec)
        {
            ec.clear();
            fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
        }
        hasBackup = !ec;
        if (!hasBackup)
            detail::removeQuietly(backup); // never leave a partial copy that could be mistaken for a save
    }
    if (stop("backup-made"))
        return false;

    fs::rename(temp, target, ec); // atomic replacement on POSIX; MoveFileEx(REPLACE_EXISTING) on Windows
    if (ec)
    {
        error = "replace save: " + ec.message();
        detail::removeQuietly(temp);
        if (hasBackup)
            detail::removeQuietly(backup);
        return false;
    }
    detail::syncDirectory(target.parent_path());
    if (stop("renamed"))
        return false;

    // The verified file now owns the name durably; the backup has done its job.
    if (hasBackup)
    {
        detail::removeQuietly(backup);
        detail::syncDirectory(target.parent_path());
    }
    return true;
}

enum class Source
{
    None,   // nothing loadable
    Primary, // the save itself
    Backup   // the save was missing or corrupt; the previous version was restored
};

// Read a save for loading. If the target is missing or fails validation and an
// intact backup from an interrupted write exists, the backup is promoted to
// the target name and returned instead.
inline Source loadWithRecovery(const fs::path &target, Bytes &image, Bytes &raw, std::string &error)
{
    std::string primaryError;
    if (readFile(target, image, primaryError) && readState(image, raw, primaryError))
        return Source::Primary;

    const auto backup = withSuffix(target, kBackupSuffix);
    std::string backupError;
    if (readFile(backup, image, backupError) && readState(image, raw, backupError))
    {
        std::error_code ec;
        fs::rename(backup, target, ec);
        if (!ec)
            detail::syncDirectory(target.parent_path());
        error = "primary save unusable (" + primaryError + "); restored previous version";
        return Source::Backup;
    }
    image.clear();
    raw.clear();
    error = primaryError;
    return Source::None;
}
} // namespace storm::save
