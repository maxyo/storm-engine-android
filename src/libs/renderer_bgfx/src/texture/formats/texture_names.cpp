// Texture name -> file candidates and container detection (see storm/render/texture_file.hpp).
#include "storm/render/texture_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace storm::render
{
namespace
{
std::string lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

bool ends_with_nocase(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && lower(text.substr(text.size() - suffix.size())) == suffix;
}

// Backslashes only, no doubled separators (names are often built as path + "\\" + file).
std::string normalise_separators(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (c == '/')
            c = '\\';
        if (c == '\\' && !out.empty() && out.back() == '\\')
            continue;
        out.push_back(c);
    }
    return out;
}
} // namespace

TextureContainer DetectTextureContainer(std::span<const uint8_t> head)
{
    if (head.size() >= 4 && memcmp(head.data(), "DDS ", 4) == 0)
        return TextureContainer::Dds;
    if (head.size() >= 8 && memcmp(head.data(), "TXX 1.0 ", 8) == 0)
        return TextureContainer::Txx;
    return TextureContainer::Tx;
}

TextureFileCandidates GetTextureFileCandidates(std::string_view name)
{
    std::string path = normalise_separators(name);
    constexpr std::string_view prefix = "resource\\textures\\";
    if (lower(path).rfind(prefix, 0) != 0)
        path = std::string(prefix) + path;

    TextureFileCandidates out;
    const bool explicit_file = ends_with_nocase(path, ".tx") || ends_with_nocase(path, ".dds") || ends_with_nocase(path, ".txx");
    out.primary = explicit_file ? path : path + ".tx";

    std::string stem = path;
    if (ends_with_nocase(stem, ".tx"))
        stem.resize(stem.size() - 3);
    for (std::string_view extension : {".tga", ".dds", ".txx"})
        if (ends_with_nocase(stem, extension))
        {
            stem.resize(stem.size() - extension.size());
            break;
        }
    const std::string primary_lower = lower(out.primary);
    for (std::string_view extension : {".tga.tx", ".dds", ".txx"})
    {
        std::string candidate = stem + std::string(extension);
        if (lower(candidate) != primary_lower)
            out.alternates.push_back(std::move(candidate));
    }
    return out;
}

} // namespace storm::render
