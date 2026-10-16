#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace storm::android::writable
{
namespace fs = std::filesystem;

inline std::string folded(std::string text)
{
    for (char &c : text)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

inline fs::path slashPath(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    // Android/POSIX native paths are UTF-8 byte strings.
    return fs::path(text);
}

inline fs::path suffixUnder(const fs::path &path, const fs::path &root)
{
    auto p = path.begin();
    for (const auto &component : root)
        if (p == path.end() || *p++ != component) return {};
    fs::path suffix;
    for (; p != path.end(); ++p) suffix /= *p;
    return suffix.empty() ? fs::path(".") : suffix;
}

inline fs::file_status entryStatus(const fs::path &path)
{
    std::error_code error;
    auto result = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw fs::filesystem_error("Inspect writable path", path, error);
    return result;
}

inline void requireDirectory(const fs::path &path, bool create)
{
    auto entry = entryStatus(path);
    if (!fs::exists(entry) && create)
    {
        fs::create_directory(path);
        entry = entryStatus(path);
    }
    if (!fs::is_directory(entry))
        throw fs::filesystem_error("Expected directory without symlink", path,
                                   std::make_error_code(std::errc::not_a_directory));
}

// Emulate the game's ASCII-insensitive paths, rejecting ambiguous duplicates.
// Never traverse a symlink, including when its exact spelling matches.
inline fs::path child(const fs::path &parent, const fs::path &name)
{
    const auto desired = folded(name.string());
    fs::path result = parent / name;
    bool matched = false;
    if (fs::is_directory(entryStatus(parent)))
    {
        for (const auto &entry : fs::directory_iterator(parent))
        {
            if (folded(entry.path().filename().string()) != desired) continue;
            if (matched)
                throw fs::filesystem_error("Ambiguous save name", parent / name,
                                           std::make_error_code(std::errc::file_exists));
            result = entry.path();
            matched = true;
        }
    }
    if (fs::is_symlink(entryStatus(result)))
        throw fs::filesystem_error("Symlink in save path", result,
                                   std::make_error_code(std::errc::too_many_symbolic_link_levels));
    return result;
}

inline fs::path contained(const fs::path &root, const fs::path &suffix)
{
    if (suffix.is_absolute())
        throw fs::filesystem_error("Save path escapes private storage", suffix,
                                   std::make_error_code(std::errc::permission_denied));
    if (fs::is_symlink(entryStatus(root)))
        throw fs::filesystem_error("Symlink at private storage root", root,
                                   std::make_error_code(std::errc::too_many_symbolic_link_levels));
    auto result = root;
    for (const auto &component : suffix)
    {
        if (component == "." || component.empty()) continue;
        if (component == "..")
        {
            if (result == root)
                throw fs::filesystem_error("Save path escapes private storage", suffix,
                                           std::make_error_code(std::errc::permission_denied));
            result = result.parent_path();
        }
        else result = child(result, component);
    }
    return result;
}

class Storage
{
public:
    Storage(fs::path game, fs::path user) : game_(std::move(game)), user_(std::move(user))
    {
        if (!game_.is_absolute() || !user_.is_absolute())
            throw std::invalid_argument("Writable storage roots must be absolute");
        game_ = game_.lexically_normal();
        user_ = user_.lexically_normal();
    }

    // This only classifies and resolves paths; unrelated resources return nullopt.
    std::optional<fs::path> resolve(const std::string &input) const
    {
        auto path = slashPath(input);
        if (path.is_absolute())
        {
            if (const auto suffix = suffixUnder(path, user_); !suffix.empty())
                return contained(user_, suffix);
            const auto suffix = suffixUnder(path, game_);
            if (suffix.empty()) return std::nullopt;
            path = suffix;
        }
        auto it = path.begin();
        while (it != path.end() && (*it == "." || it->empty())) ++it;
        if (it == path.end()) return std::nullopt;
        const auto first = folded(it->string());
        fs::path suffix;
        for (++it; it != path.end(); ++it) suffix /= *it;
        if (first == "save")
        {
            int depth = 0;
            for (const auto &part : suffix)
            {
                if (part == "..") --depth;
                else if (part != "." && !part.empty()) ++depth;
                if (depth < 0)
                    throw fs::filesystem_error("Save path escapes SaveData", path,
                                               std::make_error_code(std::errc::permission_denied));
            }
            return contained(user_, fs::path("SaveData") / suffix);
        }
        if (first == "options" && (suffix.empty() || suffix == "."))
            return contained(user_, "options");
        return std::nullopt;
    }

    // Once per selected source, never remove originals or replace private files.
    // No record is committed after any error; a later launch may retry safely.
    void migrate() const
    {
        requireDirectory(user_.parent_path(), false);
        requireDirectory(user_, true);
        requireDirectory(game_, false);
        const auto records = user_ / ".save-migrations";
        requireDirectory(records, true);
        const std::string identity = "storm-private-saves-v1\n" + game_.string() + "\n";
        uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char c : identity) { hash ^= c; hash *= 1099511628211ULL; }
        std::ostringstream name;
        name << std::hex << hash;
        const auto record = records / name.str();
        if (fs::exists(entryStatus(record)))
        {
            if (!fs::is_regular_file(entryStatus(record)))
                throw std::runtime_error("Invalid save migration record: " + record.string());
            std::ifstream input(record, std::ios::binary);
            std::ostringstream content;
            content << input.rdbuf();
            if (!input || content.str() != identity)
                throw std::runtime_error("Unreadable or conflicting save migration record: " + record.string());
            return;
        }
        const auto sourceSave = child(game_, "save");
        if (fs::exists(entryStatus(sourceSave))) copyTree(sourceSave, contained(user_, "SaveData"));
        const auto sourceOptions = child(game_, "options");
        if (fs::exists(entryStatus(sourceOptions))) copyFile(sourceOptions, contained(user_, "options"));
        publish(record, [&](int fd) { writeAll(fd, identity.data(), identity.size()); });
    }

private:
    fs::path game_, user_;

    struct Descriptor
    {
        int fd;
        ~Descriptor() { if (fd >= 0) ::close(fd); }
        void closeChecked()
        {
            const int value = fd;
            fd = -1; // close(EINTR) must not be retried on Android/Linux.
            if (::close(value)) fail("Close migration file");
        }
    };

    [[noreturn]] static void fail(const char *operation)
    {
        throw std::system_error(errno, std::generic_category(), operation);
    }

    static void writeAll(int fd, const char *data, size_t size)
    {
        while (size)
        {
            const auto written = ::write(fd, data, size);
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) fail("Write migration file");
            if (!written) throw std::runtime_error("Zero-byte migration write");
            data += written;
            size -= written;
        }
    }

    // Publish fully closed bytes atomically without replacing a concurrent file.
    static void publish(const fs::path &destination, const std::function<void(int)> &writer)
    {
        std::string pattern = (destination.parent_path() / ".storm-migrate-XXXXXX").string();
        std::vector<char> temporary(pattern.begin(), pattern.end());
        temporary.push_back('\0');
        Descriptor output{::mkstemp(temporary.data())};
        if (output.fd < 0) fail("Create migration temporary file");
        const fs::path temp(temporary.data());
        bool installed = false;
        try
        {
            writer(output.fd);
            if (::fsync(output.fd)) fail("Sync migration temporary file");
            output.closeChecked();
            if (::link(temp.c_str(), destination.c_str()))
            {
                if (errno != EEXIST) fail("Publish migration file");
                if (!fs::is_regular_file(entryStatus(destination)))
                    throw std::runtime_error("Migration destination is not a regular file: " + destination.string());
            }
            else installed = true;
            fs::remove(temp);
            Descriptor directory{::open(destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW)};
            if (directory.fd < 0) fail("Open migration directory");
            if (::fsync(directory.fd)) fail("Sync migration directory");
            directory.closeChecked();
        }
        catch (...)
        {
            std::error_code ignored;
            fs::remove(temp, ignored);
            if (installed) fs::remove(destination, ignored);
            throw;
        }
    }

    static void copyFile(const fs::path &source, const fs::path &destination)
    {
        if (!fs::is_regular_file(entryStatus(source)))
            throw std::runtime_error("Migration source is not a regular file: " + source.string());
        if (fs::exists(entryStatus(destination)))
        {
            if (!fs::is_regular_file(entryStatus(destination)))
                throw std::runtime_error("Migration destination is not a regular file: " + destination.string());
            return;
        }
        Descriptor input{::open(source.c_str(), O_RDONLY | O_NOFOLLOW)};
        if (input.fd < 0) fail("Open legacy save");
        struct stat before{}, after{};
        if (::fstat(input.fd, &before)) fail("Stat legacy save");
        if (!S_ISREG(before.st_mode)) throw std::runtime_error("Legacy save is not a regular file");
        publish(destination, [&](int output)
        {
            char buffer[65536];
            off_t copied = 0;
            for (;;)
            {
                const auto count = ::read(input.fd, buffer, sizeof(buffer));
                if (count < 0 && errno == EINTR) continue;
                if (count < 0) fail("Read legacy save");
                if (!count) break;
                writeAll(output, buffer, count);
                copied += count;
            }
            if (::fstat(input.fd, &after)) fail("Restat legacy save");
            if (copied != before.st_size || before.st_size != after.st_size ||
                before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
                throw std::runtime_error("Legacy save changed during migration: " + source.string());
            const struct timespec times[] = {before.st_atim, before.st_mtim};
            if (::futimens(output, times)) fail("Preserve legacy save timestamp");
            input.closeChecked();
        });
    }

    static void copyTree(const fs::path &source, const fs::path &destination)
    {
        requireDirectory(source, false);
        requireDirectory(destination, true);
        for (const auto &entry : fs::directory_iterator(source))
        {
            const auto target = child(destination, entry.path().filename());
            const auto type = entryStatus(entry.path());
            if (fs::is_directory(type)) copyTree(entry.path(), target);
            else if (fs::is_regular_file(type)) copyFile(entry.path(), target);
            else throw std::runtime_error("Unsupported legacy save entry (symlinks are not followed): " + entry.path().string());
        }
    }
};
} // namespace storm::android::writable
