// Lighting presets: Resource/Lighting/*.json, one set of values per hour 0..23.
#include "json_reader.hpp"

#include <charconv>

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

void ReadHour(Reader &reader, const Json &object, const std::string &path, LightingHour &hour)
{
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        if (key == "ambient" || key == "sun")
        {
            Rgb &color = key == "ambient" ? hour.ambient_color : hour.sun_color;
            float &intensity = key == "ambient" ? hour.ambient_intensity : hour.sun_intensity;
            reader.Object(value, child, [&](const std::string &k, const Json &v, const std::string &p) {
                if (k == "color")
                    reader.Color(v, p, color);
                else if (k == "intensity")
                    reader.Float(v, p, intensity);
                else
                    return false;
                return true;
            });
        }
        else if (key == "bloom")
            reader.Object(value, child, [&](const std::string &k, const Json &v, const std::string &p) {
                // the files spell threshold "treshold"
                if (k == "intensity")
                    reader.Float(v, p, hour.bloom_intensity);
                else if (k == "treshold")
                    reader.Float(v, p, hour.bloom_threshold);
                else if (k == "soft_treshold")
                    reader.Float(v, p, hour.bloom_soft_threshold);
                else
                    return false;
                return true;
            });
        else if (key == "emission")
            reader.Float(value, child, hour.emission);
        else if (key == "exposure")
            reader.Float(value, child, hour.exposure);
        else if (key == "hdr_texture")
            reader.String(value, child, hour.hdr_texture);
        else if (key == "shadow")
            reader.Object(value, child, [&](const std::string &k, const Json &v, const std::string &p) {
                return k == "ao" && (reader.Float(v, p, hour.shadow_ao), true);
            });
        else if (key == "fog")
        {
            Fog fog;
            reader.Object(value, child, [&](const std::string &k, const Json &v, const std::string &p) {
                if (k == "color")
                    reader.Color(v, p, fog.color);
                else if (k == "density")
                    reader.Float(v, p, fog.density);
                else
                    return false;
                return true;
            });
            hour.fog = fog;
        }
        else if (key == "point")
            reader.Object(value, child, [&](const std::string &group, const Json &v, const std::string &p) {
                PointLightGroup &light = hour.point_lights[group];
                reader.Object(v, p, [&](const std::string &k, const Json &x, const std::string &q) {
                    if (k == "color")
                        reader.Color(x, q, light.color);
                    else if (k == "intensity")
                        reader.Float(x, q, light.intensity);
                    else
                        return false;
                    return true;
                });
                return true;
            });
        else if (key == "sea")
        {
            SeaLighting sea;
            reader.Object(value, child, [&](const std::string &k, const Json &v, const std::string &p) {
                Rgb color{};
                if (k == "color")
                    reader.Color(v, p, sea.color);
                else if (k == "params")
                    reader.Color(v, p, sea.params);
                else if (k == "deep_color" || k == "shallow_color")
                {
                    if (reader.Color(v, p, color))
                        (k == "deep_color" ? sea.deep_color : sea.shallow_color) = color;
                }
                else if (k == "sky")
                    reader.Object(v, p, [&](const std::string &s, const Json &x, const std::string &q) {
                        return s == "color" && (reader.Color(x, q, sea.sky_color), true);
                    });
                else
                    return false;
                return true;
            });
            hour.sea = sea;
        }
        else
            return false;
        return true;
    });
}
} // namespace

LightingPreset ParseLightingPreset(std::string_view json, Report &report)
{
    Reader reader(report);
    LightingPreset preset;
    Json root;
    if (!reader.Parse(json, root))
        return preset;
    reader.Object(root, "$", [&](const std::string &key, const Json &value, const std::string &path) {
        float number = 0;
        if (key == "schemaVersion")
            reader.Int(value, path, preset.schema_version);
        else if (key == "presetName")
            reader.String(value, path, preset.preset_name);
        else if (key == "description")
            reader.String(value, path, preset.description);
        else if (key == "grass_preset")
            reader.String(value, path, preset.grass_preset);
        else if (key == "locationId")
            reader.StringList(value, path, preset.location_ids);
        else if (key == "sun_azimuth" || key == "sun_height")
        {
            if (reader.Float(value, path, number))
                (key == "sun_azimuth" ? preset.sun_azimuth : preset.sun_height) = number;
        }
        else if (key == "good_rays") // sic: god rays
        {
            GodRays rays;
            reader.Object(value, path, [&](const std::string &k, const Json &v, const std::string &p) {
                if (k == "enabled")
                    reader.Bool(v, p, rays.enabled);
                else if (k == "density")
                    reader.Float(v, p, rays.density);
                else if (k == "intensity")
                    reader.Float(v, p, rays.intensity);
                else if (k == "length")
                    reader.Float(v, p, rays.length);
                else if (k == "quality")
                    reader.Int(v, p, rays.quality);
                else
                    return false;
                return true;
            });
            preset.god_rays = rays;
        }
        else if (key == "hours")
            reader.Object(value, path, [&](const std::string &k, const Json &v, const std::string &p) {
                int hour = -1;
                const auto [end, error] = std::from_chars(k.data(), k.data() + k.size(), hour);
                if (error != std::errc() || end != k.data() + k.size() || hour < 0 || hour > 23)
                {
                    reader.Error(p, "hour key must be 0..23");
                    return true;
                }
                LightingHour parsed;
                ReadHour(reader, v, p, parsed);
                preset.hours[size_t(hour)] = std::move(parsed);
                return true;
            });
        else
            return false;
        return true;
    });
    return preset;
}

} // namespace storm::scene_data
