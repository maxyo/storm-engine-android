#include "picture_atlas.h"

#include <cstdlib>
#include <vector>

namespace storm::ui
{

namespace
{

std::string Trim(std::string_view text)
{
    const size_t first = text.find_first_not_of(" \t\r"), last = text.find_last_not_of(" \t\r");
    return first == std::string_view::npos ? std::string() : std::string(text.substr(first, last - first + 1));
}

std::string Upper(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}

// "..\BATTLE_INTERFACE\land_icon.tga" → "BATTLE_INTERFACE/land_icon" under INTERFACES/.
std::string TextureOf(std::string name)
{
    for (char &c : name)
        if (c == '\\')
            c = '/';
    if (name.size() > 4 && Upper(name.substr(name.size() - 4)) == ".TGA")
        name.resize(name.size() - 4);
    std::vector<std::string> parts = {"INTERFACES"};
    size_t at = 0;
    while (at <= name.size())
    {
        size_t end = name.find('/', at);
        if (end == std::string::npos)
            end = name.size();
        const std::string part = name.substr(at, end - at);
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        at = end + 1;
    }
    std::string out;
    for (const std::string &part : parts)
        out += (out.empty() ? "" : "/") + part;
    return out;
}

} // namespace

size_t PictureAtlas::Load(std::string_view ini)
{
    pictures_.clear();
    std::string group, texture;
    float width = 0.0f, height = 0.0f;
    size_t at = 0;
    while (at < ini.size())
    {
        size_t end = ini.find('\n', at);
        if (end == std::string_view::npos)
            end = ini.size();
        std::string line = Trim(ini.substr(at, end - at));
        at = end + 1;
        if (const size_t comment = line.find(';'); comment != std::string::npos)
            line = Trim(line.substr(0, comment));
        if (line.empty())
            continue;
        if (line.front() == '[')
        {
            const size_t close = line.find(']');
            group = Upper(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
            texture.clear();
            width = height = 0.0f;
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = Upper(Trim(line.substr(0, equals)));
        const std::string value = Trim(line.substr(equals + 1));
        if (key == "STEXTURENAME")
            texture = TextureOf(value);
        else if (key == "WTEXTUREWIDTH")
            width = static_cast<float>(std::atof(value.c_str()));
        else if (key == "WTEXTUREHEIGHT")
            height = static_cast<float>(std::atof(value.c_str()));
        else if (key == "PICTURE" && !texture.empty() && width > 0.0f && height > 0.0f)
        {
            // name,left,top,right,bottom in texture pixels
            std::vector<std::string> fields;
            size_t from = 0;
            while (from <= value.size())
            {
                size_t comma = value.find(',', from);
                if (comma == std::string::npos)
                    comma = value.size();
                fields.push_back(Trim(value.substr(from, comma - from)));
                from = comma + 1;
            }
            if (fields.size() < 5 || fields[0].empty())
                continue;
            AtlasPicture picture;
            picture.texture = texture;
            picture.uv[0] = static_cast<float>(std::atof(fields[1].c_str())) / width;
            picture.uv[1] = static_cast<float>(std::atof(fields[2].c_str())) / height;
            picture.uv[2] = static_cast<float>(std::atof(fields[3].c_str())) / width;
            picture.uv[3] = static_cast<float>(std::atof(fields[4].c_str())) / height;
            pictures_.emplace(group + "/" + Upper(fields[0]), std::move(picture));
        }
    }
    return pictures_.size();
}

const AtlasPicture *PictureAtlas::Find(std::string_view group, std::string_view picture) const
{
    const auto found = pictures_.find(Upper(group) + "/" + Upper(picture));
    return found == pictures_.end() ? nullptr : &found->second;
}

} // namespace storm::ui
