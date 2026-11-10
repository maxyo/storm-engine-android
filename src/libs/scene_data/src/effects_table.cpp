// Resource/Effects/effects.json: effect name -> {file, speed}; string values are section separators.
#include "json_reader.hpp"

namespace storm::scene_data
{

EffectsTable ParseEffectsTable(std::string_view json, Report &report)
{
    detail::Reader reader(report);
    EffectsTable table;
    detail::Json root;
    if (!reader.Parse(json, root))
        return table;
    reader.Object(root, "$", [&](const std::string &name, const detail::Json &value, const std::string &path) {
        if (value.is_string())
        {
            table.sections.push_back(name);
            return true;
        }
        EffectEntry entry;
        bool has_file = false;
        reader.Object(value, path, [&](const std::string &key, const detail::Json &v, const std::string &p) {
            if (key == "file")
                has_file = reader.String(v, p, entry.file);
            else if (key == "speed")
                reader.Float(v, p, entry.speed);
            else
                return false;
            return true;
        });
        if (!has_file)
            reader.Error(path, "effect without a file");
        else
            table.effects[name] = std::move(entry);
        return true;
    });
    return table;
}

} // namespace storm::scene_data
