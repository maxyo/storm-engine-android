// Resource/texture_library.json: texture sets for grass, blood, blots, sails, ropes, flags, clouds.
#include "json_reader.hpp"

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

void ReadEntry(Reader &reader, const Json &object, const std::string &path, TextureLibraryEntry &entry)
{
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        if (key == "albedo_texture")
            reader.String(value, child, entry.albedo_texture);
        else if (key == "rma_texture")
            reader.String(value, child, entry.rma_texture);
        else if (key == "normal_texture")
            reader.String(value, child, entry.normal_texture);
        else if (key == "lighting_texture")
            reader.String(value, child, entry.lighting_texture);
        else if (value.is_boolean())
            entry.flags[key] = value.get<bool>();
        else if (value.is_number())
            entry.numbers[key] = value.get<float>();
        else if (value.is_array())
            reader.Color(value, child, entry.colors[key]);
        else if (value.is_object())
            ReadEntry(reader, value, child, entry.layers[key]);
        else
            return false;
        return true;
    });
}
} // namespace

const TextureLibraryEntry *TextureLibrary::Find(std::string_view preset, std::string_view variant) const
{
    const auto group = presets.find(std::string(preset));
    if (group == presets.end())
        return nullptr;
    const auto entry = group->second.find(std::string(variant));
    return entry == group->second.end() ? nullptr : &entry->second;
}

TextureLibrary ParseTextureLibrary(std::string_view json, Report &report)
{
    Reader reader(report);
    TextureLibrary library;
    Json root;
    if (!reader.Parse(json, root))
        return library;
    reader.Object(root, "$", [&](const std::string &preset, const Json &value, const std::string &path) {
        reader.Object(value, path, [&](const std::string &variant, const Json &entry, const std::string &child) {
            ReadEntry(reader, entry, child, library.presets[preset][variant]);
            return true;
        });
        return true;
    });
    return library;
}

} // namespace storm::scene_data
