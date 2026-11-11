// Caribbean Legend: per-object PBR materials from the sidecar <model>.json next to the .gm (renderer_bgfx/pbr).
#include "pbr_materials.hpp"

#include "storm/pbr/material.hpp"
#include "storm/render/pbr.hpp"
#include "storm/scene_data/scene_data.hpp"
#include "v_file_service.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <vector>

namespace storm::geometry
{
namespace
{
// Owns the materials of one GEOS and the textures they reference.
struct PbrSet
{
    VDX9RENDER *render = nullptr;
    std::vector<std::unique_ptr<render::PbrMaterial>> materials;
    std::vector<int32_t> textures;
    ~PbrSet()
    {
        for (const int32_t id : textures)
            render->TextureRelease(id);
    }
};

bool ReadText(const std::string &path, std::string &out)
{
    auto file = fio->_CreateFile(path.c_str(), std::ios::binary | std::ios::in);
    if (!file.is_open())
        return false;
    out.resize(fio->_GetFileSize(path.c_str()));
    const bool ok = out.empty() || fio->_ReadFile(file, out.data(), static_cast<std::streamsize>(out.size()));
    fio->_CloseFile(file);
    return ok;
}
} // namespace

void AttachPbrMaterials(GEOS &geo, const char *model, VDX9RENDER &render)
{
    const std::string path = std::string("resource\\models\\") + model + ".json";
    if (!fio->_FileOrDirectoryExists(path.c_str()))
        return; // no sidecar: the model has no PBR materials
    std::string json;
    if (!ReadText(path, json))
    {
        spdlog::error("pbr: cannot read {}", path);
        return;
    }
    scene_data::Report report;
    const scene_data::ModelSidecar sidecar = scene_data::ParseModelSidecar(json, report);
    for (const auto &error : report.errors)
        spdlog::error("pbr: {}: {}", path, error);
    if (!report.unknown_keys.empty())
    {
        std::string keys;
        for (const auto &key : report.unknown_keys)
            keys += (keys.empty() ? "" : ", ") + key;
        spdlog::info("pbr: {}: keys without a PBR meaning here: {}", path, keys);
    }

    auto set = std::make_shared<PbrSet>();
    set->render = &render;
    GEOS::INFO info;
    geo.GetInfo(info);
    std::vector<const render::PbrMaterial *> per_object(size_t(info.nobjects), nullptr);
    int pbr = 0, rejected = 0, plain = 0;
    for (int32_t o = 0; o < info.nobjects; ++o)
    {
        GEOS::OBJECT object;
        geo.GetObj(o, object);
        const auto entry = object.name ? sidecar.models.find(object.name) : sidecar.models.end();
        if (entry == sidecar.models.end())
            continue;
        const std::string name = std::string(model) + ":" + object.name;
        pbr::ResolveResult resolved = pbr::ResolveMaterial(entry->second);
        if (resolved.resolution == pbr::Resolution::NotPbr)
        {
            ++plain;
            continue;
        }
        if (resolved.resolution == pbr::Resolution::Rejected)
        {
            for (const auto &error : resolved.errors)
                spdlog::error("pbr: {}: {}; drawn with the legacy material", name, error);
            ++rejected;
            continue;
        }
        auto material = std::make_unique<render::PbrMaterial>();
        material->desc = std::move(resolved.material);
        material->name = name;
        bool complete = true;
        for (const pbr::TextureRef &ref : material->desc.textures)
        {
            const int32_t id = render.TextureCreate(ref.path.c_str());
            IDirect3DBaseTexture9 *texture = id >= 0 ? render.GetBaseTexture(id) : nullptr;
            if (id >= 0)
                set->textures.push_back(id);
            if (!texture)
            {
                spdlog::error("pbr: {}: {}: texture \"{}\" cannot be loaded; drawn with the legacy material", name, ref.key, ref.path);
                complete = false;
                break;
            }
            material->textures[ref.unit] = texture;
        }
        if (!complete)
        {
            ++rejected;
            continue;
        }
        per_object[size_t(o)] = material.get();
        set->materials.push_back(std::move(material));
        ++pbr;
    }
    spdlog::info("pbr: {}: {} objects PBR, {} rejected, {} without PBR inputs", model, pbr, rejected, plain);
    if (pbr > 0)
        geo.AttachPbrMaterials(std::move(per_object), std::move(set));
}

} // namespace storm::geometry
