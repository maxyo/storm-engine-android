#pragma once

// Case-insensitive resource path index for case-sensitive filesystems.
//
// The game refers to files with arbitrary case and either relative to the
// working directory ("resource\\ini\\x.ini") or through the absolute game
// directory. The index maps both lowercase spellings to the real on-disk path.
//
// scan() used to be two full recursive walks, one of "." and one of the
// executable/game directory. On Android those are the same directory, so
// startup paid for the whole tree twice. scan() now walks once and derives
// both key families from each entry whenever the two roots are the same
// directory, and falls back to the two walks otherwise.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace storm::resource_index
{
using Map = std::unordered_map<std::string, std::string>;

inline std::string lowered(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// `path` as the directory iterator reports it, `prefix` already lowercase.
inline void add(Map &map, const std::string &path, const std::string &prefix)
{
    auto key = lowered(path);
    const bool below = key.compare(0, prefix.size(), prefix) == 0;
    const auto under = [&](std::string_view directory) {
        return below && key.compare(prefix.size(), directory.size(), directory) == 0;
    };
    const bool ini = key.size() >= 4 && key.compare(key.size() - 4, 4, ".ini") == 0;
    if (under("program") || under("resource") || under("save") || ini)
        map[std::move(key)] = path;
}

inline bool indexable(const std::filesystem::directory_entry &entry)
{
    return entry.is_regular_file() || entry.is_directory();
}

// Reference behaviour: relative keys from the working directory.
inline void scanRelative(Map &map)
{
    for (const auto &entry : std::filesystem::recursive_directory_iterator("."))
    {
        if (!indexable(entry))
            continue;
        auto path = entry.path().string();
        if (path.rfind("./", 0) == 0)
            path.erase(0, 2);
        add(map, path, "");
    }
}

// Reference behaviour: absolute keys below `root` (spelled as the caller passes it).
inline void scanAbsolute(Map &map, const std::string &root)
{
    const auto prefix = lowered(root);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        if (indexable(entry))
            add(map, entry.path().string(), prefix);
}

// `root` must end with a path separator (FILE_SERVICE::_GetExecutableDirectory does).
// Returns the number of directory walks performed (1 or 2), for logging/tests.
inline int scan(Map &map, const std::string &root)
{
    std::error_code error;
    const bool same = !root.empty() && root.back() == '/' && std::filesystem::equivalent(".", root, error) && !error;
    if (!same)
    {
        scanRelative(map);
        scanAbsolute(map, root);
        return 2;
    }
    const auto prefix = lowered(root);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!indexable(entry))
            continue;
        const auto absolute = entry.path().string();
        add(map, absolute.substr(root.size()), "");
        add(map, absolute, prefix);
    }
    return 1;
}
} // namespace storm::resource_index
