#include "storm/pbr/material.hpp"

#include <cmath>

namespace storm::pbr
{

const char *VariantName(Variant variant)
{
    switch (variant)
    {
    case Variant::Standard: return "standard";
    case Variant::Flat: return "flat";
    case Variant::Emissive: return "emissive";
    case Variant::Layer: return "layer";
    case Variant::Count: break;
    }
    return "?";
}

uint8_t LayerAlbedoUnit(int layer)
{
    static constexpr uint8_t units[3] = {kUnitLayerAlbedo, 8, 9};
    return units[layer];
}

namespace
{
bool ValidFactor(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool ValidTiling(float value)
{
    return std::isfinite(value) && value > 0.0f;
}

// Adds the texture of `key` at `unit`; an empty or unusable reference is an error when `required`.
void AddTexture(ResolveResult &out, const std::string &reference, const char *key, uint8_t unit, bool required)
{
    if (reference.empty())
    {
        if (required)
            out.errors.push_back(std::string(key) + ": missing");
        return;
    }
    std::string path = scene_data::TexturePath(reference);
    if (path.empty())
    {
        out.errors.push_back(std::string(key) + ": not a texture reference: \"" + reference + "\"");
        return;
    }
    out.material.textures.push_back({unit, key, std::move(path)});
}
} // namespace

ResolveResult ResolveMaterial(const scene_data::ModelMaterial &entry)
{
    ResolveResult out;
    const bool declares = !entry.rma_texture.empty() || !entry.normal_texture.empty() || entry.layer_material ||
                          !entry.emission_texture.empty() || entry.roughness || entry.metallic;
    if (!declares)
        return out; // NotPbr

    MaterialDesc &m = out.material;
    m.alpha = entry.alpha || entry.alpha_ccw;
    if (entry.roughness)
    {
        if (ValidFactor(*entry.roughness))
            m.roughness = *entry.roughness;
        else
            out.errors.push_back("roughness: must be 0..1, is " + std::to_string(*entry.roughness));
    }
    if (entry.metallic)
    {
        if (ValidFactor(*entry.metallic))
            m.metallic = *entry.metallic;
        else
            out.errors.push_back("metallic: must be 0..1, is " + std::to_string(*entry.metallic));
    }
    if (entry.emission)
    {
        if (std::isfinite(*entry.emission) && *entry.emission >= 0.0f)
            m.emission = *entry.emission;
        else
            out.errors.push_back("emission: must be >= 0, is " + std::to_string(*entry.emission));
    }

    if (entry.rma_texture.empty())
    {
        // Constant material: both channels must come from the overrides (AO is 1).
        if (!entry.roughness || !entry.metallic)
            out.errors.push_back("rma_texture: missing, and roughness / metallic are not both given");
        m.needs_white = true;
    }
    else
        AddTexture(out, entry.rma_texture, "rma_texture", kUnitRma, true);

    if (entry.layer_material)
    {
        m.variant = Variant::Layer;
        AddTexture(out, entry.normal_texture, "normal_texture", kUnitNormal, true);
        AddTexture(out, entry.mask_texture, "mask_texture", kUnitMask, true);
        if (!entry.emission_texture.empty())
            out.errors.push_back("emission_texture: not supported on a layer material");
        static constexpr const char *names[3][3] = {{"r_albedo_texture", "r_rma_texture", "r_normal_texture"},
                                                    {"g_albedo_texture", "g_rma_texture", "g_normal_texture"},
                                                    {"b_albedo_texture", "b_rma_texture", "b_normal_texture"}};
        static constexpr const char *tiling_names[3] = {"r_tiling", "g_tiling", "b_tiling"};
        for (int i = 0; i < 3; ++i)
        {
            const scene_data::LayerTextures &layer = entry.layers[size_t(i)];
            AddTexture(out, layer.albedo, names[i][0], LayerAlbedoUnit(i), true);
            AddTexture(out, layer.rma, names[i][1], uint8_t(kUnitLayerRma + i), true);
            AddTexture(out, layer.normal, names[i][2], uint8_t(kUnitLayerNormal + i), true);
            if (!ValidTiling(layer.tiling))
                out.errors.push_back(std::string(tiling_names[i]) + ": must be > 0, is " + std::to_string(layer.tiling));
            m.tiling[size_t(i) + 1] = layer.tiling;
        }
        if (!ValidTiling(entry.base_tiling))
            out.errors.push_back("base_tiling: must be > 0, is " + std::to_string(entry.base_tiling));
        m.tiling[0] = entry.base_tiling;
        if (!std::isfinite(entry.normal_strength) || entry.normal_strength < 0.0f)
            out.errors.push_back("normal_strength: must be >= 0, is " + std::to_string(entry.normal_strength));
        m.normal_strength = entry.normal_strength;
    }
    else
    {
        if (!entry.mask_texture.empty())
            out.errors.push_back("mask_texture: given without layer_material");
        if (!entry.emission_texture.empty())
        {
            m.variant = Variant::Emissive;
            AddTexture(out, entry.normal_texture, "normal_texture", kUnitNormal, true);
            AddTexture(out, entry.emission_texture, "emission_texture", kUnitEmission, true);
            if (!entry.emission)
                m.emission = 1.0f;
        }
        else if (!entry.normal_texture.empty())
        {
            m.variant = Variant::Standard;
            AddTexture(out, entry.normal_texture, "normal_texture", kUnitNormal, true);
        }
        else
            m.variant = Variant::Flat;
    }

    out.resolution = out.errors.empty() ? Resolution::Pbr : Resolution::Rejected;
    return out;
}

} // namespace storm::pbr
