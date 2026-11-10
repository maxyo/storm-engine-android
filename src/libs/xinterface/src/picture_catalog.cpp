#include "picture_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

namespace storm::xinterface
{
namespace
{
std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
} // namespace

bool parsePicture(std::string_view value, Picture &picture)
{
    const std::string text(value);
    std::string name(text.size() + 1, '\0');
    int left = 0, top = 0, right = 0, bottom = 0;
    if (std::sscanf(text.c_str(), "%[^,],%d,%d,%d,%d", name.data(), &left, &top, &right, &bottom) != 5)
        return false;
    name.resize(std::char_traits<char>::length(name.c_str()));
    picture = {std::move(name), left, top, right, bottom};
    return true;
}

void PictureCatalog::add(std::string_view source, const std::vector<PictureIniSection> &sections)
{
    std::unordered_set<std::string> known;
    for (const auto &group : groups_)
        known.insert(lower(group.name));
    for (const auto &section : sections)
    {
        if (!known.insert(lower(section.name)).second)
            throw std::runtime_error("picture list " + std::string(source) + ": group [" + section.name +
                                     "] is already defined");
        PictureGroup group{section.name, section.texture, section.width, section.height,
                           static_cast<int32_t>(pictures_.size()), static_cast<int32_t>(section.pictures.size())};
        for (const auto &value : section.pictures)
        {
            Picture picture;
            if (!parsePicture(value, picture))
                throw std::runtime_error("picture list " + std::string(source) + ": group [" + section.name +
                                         "]: malformed picture '" + value + "' (want name,left,top,right,bottom)");
            pictures_.push_back(std::move(picture));
        }
        groups_.push_back(std::move(group));
    }
}

std::vector<std::string> pictureIniFiles(GameProfile profile,
                                         const std::function<std::vector<std::string>(const std::string &)> &listIni)
{
    if (profile == GameProfile::TEHO)
        return {"resource\\ini\\interfaces\\pictures.ini"};

    const std::string directory = "resource\\ini\\interfaces\\pictures";
    auto files = listIni(directory);
    if (files.empty())
        throw std::runtime_error("no picture lists (*.ini) under " + directory);
    for (auto &file : files)
        std::ranges::replace(file, '/', '\\');
    std::ranges::sort(files, [](const std::string &a, const std::string &b) { return lower(a) < lower(b); });
    for (auto &file : files)
        file = directory + "\\" + file;
    return files;
}
} // namespace storm::xinterface
