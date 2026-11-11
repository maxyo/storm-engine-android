// Resource/INI/aces.json: the camera's tone mapping and colour grade.
#include "json_reader.hpp"

namespace storm::scene_data
{

ToneMapping ParseToneMapping(std::string_view json, Report &report)
{
    detail::Reader reader(report);
    ToneMapping out;
    detail::Json root;
    if (!reader.Parse(json, root))
        return out;
    reader.Object(root, "$", [&](const std::string &key, const detail::Json &value, const std::string &path) {
        if (key == "type")
            reader.String(value, path, out.type);
        else if (key == "version")
            reader.Int(value, path, out.version);
        else if (key == "exposure_ev")
            reader.Float(value, path, out.exposure_ev);
        else if (key == "contrast")
            reader.Float(value, path, out.contrast);
        else if (key == "saturation")
            reader.Float(value, path, out.saturation);
        else if (key == "temperature")
            reader.Float(value, path, out.temperature);
        else if (key == "tint")
            reader.Float(value, path, out.tint);
        else if (key == "color_filter")
            reader.Color(value, path, out.color_filter);
        else if (key == "lift")
            reader.Color(value, path, out.lift);
        else if (key == "gamma")
            reader.Color(value, path, out.gamma);
        else if (key == "gain")
            reader.Color(value, path, out.gain);
        else
            return false;
        return true;
    });
    return out;
}

} // namespace storm::scene_data
