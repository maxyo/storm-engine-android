// Per-model sidecar materials: Resource/Models/**/<model>.json next to <model>.gm.
#include "json_reader.hpp"

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

bool ReadLayerField(Reader &reader, ModelMaterial &material, const std::string &key, const Json &value,
                    const std::string &path)
{
    // r_albedo_texture, g_normal_texture, b_rma_texture, r_tiling ...
    if (key.size() < 3 || key[1] != '_')
        return false;
    const size_t index = key[0] == 'r' ? 0 : key[0] == 'g' ? 1 : key[0] == 'b' ? 2 : 3;
    if (index == 3)
        return false;
    LayerTextures &layer = material.layers[index];
    const std::string field = key.substr(2);
    if (field == "albedo_texture")
        reader.String(value, path, layer.albedo);
    else if (field == "normal_texture")
        reader.String(value, path, layer.normal);
    else if (field == "rma_texture")
        reader.String(value, path, layer.rma);
    else if (field == "tiling")
        reader.Float(value, path, layer.tiling);
    else
        return false;
    return true;
}

void ReadMaterial(Reader &reader, const Json &object, const std::string &path, ModelMaterial &material)
{
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        float number = 0;
        if (key == "reflection")
            reader.Bool(value, child, material.reflection);
        else if (key == "alpha")
            reader.Bool(value, child, material.alpha);
        else if (key == "alpha_ccw")
            reader.Bool(value, child, material.alpha_ccw);
        else if (key == "shadow_cw")
            reader.Bool(value, child, material.shadow_cw);
        else if (key == "point_shadow")
            reader.Bool(value, child, material.point_shadow);
        else if (key == "PointLight")
            reader.Bool(value, child, material.point_light);
        else if (key == "layer_material")
            reader.Bool(value, child, material.layer_material);
        else if (key == "rma_texture")
            reader.String(value, child, material.rma_texture);
        else if (key == "normal_texture")
            reader.String(value, child, material.normal_texture);
        else if (key == "emission_texture")
            reader.String(value, child, material.emission_texture);
        else if (key == "mask_texture")
            reader.String(value, child, material.mask_texture);
        else if (key == "path_preset")
            reader.String(value, child, material.path_preset);
        else if (key == "animation")
            reader.String(value, child, material.animation);
        else if (key == "base_tiling")
            reader.Float(value, child, material.base_tiling);
        else if (key == "normal_strength")
            reader.Float(value, child, material.normal_strength);
        else if (key == "emission" || key == "metallic" || key == "roughness")
        {
            if (reader.Float(value, child, number))
                (key == "emission" ? material.emission : key == "metallic" ? material.metallic : material.roughness) = number;
        }
        else
            return ReadLayerField(reader, material, key, value, child);
        return true;
    });
}
} // namespace

ModelSidecar ParseModelSidecar(std::string_view json, Report &report)
{
    Reader reader(report);
    ModelSidecar sidecar;
    Json root;
    if (!reader.Parse(json, root))
        return sidecar;
    reader.Object(root, "$", [&](const std::string &key, const Json &value, const std::string &path) {
        if (key != "models")
            return false;
        reader.Object(value, path, [&](const std::string &name, const Json &material, const std::string &child) {
            ReadMaterial(reader, material, child, sidecar.models[name]);
            return true;
        });
        return true;
    });
    return sidecar;
}

} // namespace storm::scene_data
