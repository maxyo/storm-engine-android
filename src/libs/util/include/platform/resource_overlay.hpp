#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace storm::android
{
inline std::string resourceKey(const std::filesystem::path &path)
{
    auto key = path.generic_string();
    for (auto &c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

// Only UI source/configuration is eligible. Never redirect models, textures,
// audio, saves, engine settings or arbitrary executable files into this layer.
inline bool isUiOverlayPath(const std::filesystem::path &relative)
{
    if (relative.is_absolute()) return false;
    for (const auto &part : relative) if (part == "..") return false;
    const auto key = resourceKey(relative);
    const auto extension = resourceKey(relative.extension());
    return ((key.starts_with("program/interface/") || key.starts_with("program/controls/")) &&
            (extension == ".c" || extension == ".h")) ||
           (key.starts_with("resource/ini/interfaces/") && extension == ".ini");
}

inline bool isEngineShaderOverlayPath(const std::filesystem::path &relative)
{
    if (relative.is_absolute()) return false;
    for (const auto &part : relative) if (part == "..") return false;
    const auto key = resourceKey(relative);
    const auto extension = resourceKey(relative.extension());
    return key.starts_with("resource/techniques/") &&
        (extension == ".vso" || extension == ".pso" || extension == ".fx" || extension == ".h" || extension == ".sha");
}

inline void indexUiOverlay(const std::filesystem::path &root,
                           std::unordered_map<std::string, std::string> &paths, bool shaders = false)
{
    if (!std::filesystem::is_directory(root)) return;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_symlink() || !entry.is_regular_file()) continue;
        const auto relative = entry.path().lexically_relative(root);
        if (shaders ? isEngineShaderOverlayPath(relative) : isUiOverlayPath(relative)) paths[resourceKey(relative)] = entry.path().string();
    }
}
} // namespace storm::android
