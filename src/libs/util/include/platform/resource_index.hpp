#pragma once

// Case-insensitive resource path index for case-sensitive filesystems.
//
// The game refers to files with arbitrary case and either relative to the
// working directory ("resource\\ini\\x.ini") or through the absolute game
// directory. The index maps both lowercase spellings to the real on-disk path.
// The working directory is the game directory (Android and the Linux host), so
// one walk of the game directory derives both key families from each entry.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
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

// `root` is the game directory with a trailing separator
// (FILE_SERVICE::_GetExecutableDirectory) and must be the working directory.
inline void scan(Map &map, const std::string &root)
{
    std::error_code error;
    if (root.empty() || root.back() != '/' || !std::filesystem::equivalent(".", root, error) || error)
        throw std::runtime_error("The resource index root '" + root + "' is not the working directory");
    const auto prefix = lowered(root);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!indexable(entry))
            continue;
        const auto absolute = entry.path().string();
        add(map, absolute.substr(root.size()), "");
        add(map, absolute, prefix);
    }
}
} // namespace storm::resource_index
