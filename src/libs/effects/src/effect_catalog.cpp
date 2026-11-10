#include "effect_catalog.hpp"

#include "storm/scene_data/scene_data.hpp"
#include "string_compare.hpp"
#include "v_file_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>

namespace storm::effects
{
namespace
{
std::string Lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && storm::iEquals(text.substr(text.size() - suffix.size()), suffix);
}
} // namespace

std::string EffectRecord::EffectPath() const
{
    return std::string(kEffectsDirectory) + file;
}

bool ReadGameFile(const std::string &path, std::string &data)
{
    auto file = fio->_CreateFile(path.c_str(), std::ios::binary | std::ios::in);
    if (!file.is_open())
        return false;
    const auto size = fio->_GetFileSize(path.c_str());
    data.resize(size);
    const bool ok = size == 0 || fio->_ReadFile(file, data.data(), static_cast<std::streamsize>(size));
    fio->_CloseFile(file);
    return ok;
}

bool EffectCatalog::Reload()
{
    loaded_ = true;
    records_.clear();
    std::string json;
    if (!ReadGameFile(std::string(kEffectsTable), json))
    {
        spdlog::error("effects: cannot read {}; effect ids are unavailable", kEffectsTable);
        return false;
    }
    scene_data::Report report;
    const auto table = scene_data::ParseEffectsTable(json, report);
    for (const auto &error : report.errors)
        spdlog::error("effects: {}: {}", kEffectsTable, error);
    for (const auto &key : report.unknown_keys)
        spdlog::warn("effects: {}: unknown key {}", kEffectsTable, key);
    for (const auto &[id, entry] : table.effects)
    {
        EffectRecord record;
        record.file = entry.file;
        record.speed = entry.speed;
        if (EndsWith(entry.file, ".xps"))
            record.kind = EffectRecord::Kind::particle_system;
        else if (EndsWith(entry.file, ".efkefc"))
            record.kind = EffectRecord::Kind::effekseer;
        else
        {
            spdlog::error("effects: {}: effect '{}' names '{}', which is neither .efkefc nor .xps; skipped",
                          kEffectsTable, id, entry.file);
            continue;
        }
        if (!records_.emplace(Lower(id), std::move(record)).second)
            spdlog::error("effects: {}: effect id '{}' differs from another one only by case; the first is used",
                          kEffectsTable, id);
    }
    spdlog::info("effects: {} effect ids from {}", records_.size(), kEffectsTable);
    return report.ok();
}

void EffectCatalog::EnsureLoaded()
{
    if (!loaded_)
        Reload();
}

std::optional<EffectRecord> EffectCatalog::Find(std::string_view id)
{
    EnsureLoaded();
    const auto found = records_.find(Lower(id));
    if (found == records_.end())
        return std::nullopt;
    return found->second;
}

const std::map<std::string, EffectRecord> &EffectCatalog::Records()
{
    EnsureLoaded();
    return records_;
}

EffectCatalog &Catalog()
{
    static EffectCatalog catalog;
    return catalog;
}

} // namespace storm::effects
