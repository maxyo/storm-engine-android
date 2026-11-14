// The texture name rules of the derived maps (normal_map.cpp relief classes, gloss_map.cpp gloss classes): one rule
// matches a normalized texture path (NormalizeTexturePath) by its folder and its file name. Internal to fx.
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace storm::fx::rules
{

enum class Match : uint8_t
{
    Folder,   // the path starts with the pattern (a folder, "ships/"); the name is not looked at
    Prefix,   // the file name starts with the pattern
    Contains, // the file name contains the pattern
    Exact,    // the file name equals the pattern
};

// `folder`: only paths in this folder ("" = any).
inline bool Matches(std::string_view path, const char *folder, Match match, const char *pattern)
{
    if (path.compare(0, std::strlen(folder), folder) != 0)
        return false;
    const size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    switch (match)
    {
    case Match::Exact:
        return name == pattern;
    case Match::Folder:
        return true;
    case Match::Prefix:
        return name.compare(0, std::strlen(pattern), pattern) == 0;
    case Match::Contains:
        return name.find(pattern) != std::string_view::npos;
    }
    return false;
}

} // namespace storm::fx::rules
