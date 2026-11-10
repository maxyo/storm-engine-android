// Caribbean Legend: the lighting preset of the current scene and hour for the PBR renderer (storm/render/pbr.hpp).
#include "pbr_lighting.hpp"

#include "core.h"
#include "entity.h"
#include "fs.h"
#include "storm/pbr/environment.hpp"
#include "storm/render/pbr.hpp"
#include "storm/scene_data/scene_data.hpp"
#include "v_file_service.h"

#include <spdlog/spdlog.h>

#include <set>
#include <string>

namespace storm::weather
{
namespace
{
bool ReadGameFile(const std::string &path, std::string &out)
{
    auto file = fio->_CreateFile(path.c_str(), std::ios::binary | std::ios::in);
    if (!file.is_open())
        return false;
    out.resize(fio->_GetFileSize(path.c_str()));
    const bool ok = out.empty() || fio->_ReadFile(file, out.data(), static_cast<std::streamsize>(out.size()));
    fio->_CloseFile(file);
    return ok;
}

struct State
{
    bool loaded = false;
    pbr::LightingLibrary library;
    pbr::Grade grade;
    std::set<std::string> reported;
};

State &Shared()
{
    static State state;
    return state;
}

void ReportOnce(State &state, const std::string &key, const std::string &message)
{
    if (state.reported.insert(key).second)
        spdlog::error("pbr: {}", message);
}

void Load(State &state)
{
    state.loaded = true;
    render::ConfigurePbr({.cache_directory = fs::GetStashPath() / "PbrCache", .read_file = [](const std::string &path) {
                              std::string text;
                              if (!ReadGameFile(path, text))
                                  return std::vector<uint8_t>{};
                              return std::vector<uint8_t>(text.begin(), text.end());
                          }});
    const auto files = fio->_GetFsPathsByMask("resource\\lighting", "*.json", true, false, true, true);
    for (const auto &file : files)
    {
        std::string text;
        if (!ReadGameFile(file.string(), text))
        {
            spdlog::error("pbr: cannot read {}", file.string());
            continue;
        }
        scene_data::Report report;
        scene_data::LightingPreset preset = scene_data::ParseLightingPreset(text, report);
        for (const auto &error : report.errors)
            spdlog::error("pbr: {}: {}", file.string(), error);
        for (const auto &error : state.library.Add(file.filename().string(), std::move(preset)))
            spdlog::error("pbr: {}", error);
    }
    spdlog::info("pbr: {} lighting presets", state.library.size());

    constexpr const char *kToneMapping = "resource\\ini\\aces.json";
    std::string text;
    if (!ReadGameFile(kToneMapping, text))
    {
        spdlog::error("pbr: cannot read {}; PBR objects are tone mapped without the game's grade", kToneMapping);
        return;
    }
    scene_data::Report report;
    const scene_data::ToneMapping tone = scene_data::ParseToneMapping(text, report);
    for (const auto &error : report.errors)
        spdlog::error("pbr: {}: {}", kToneMapping, error);
    for (const auto &key : report.unknown_keys)
        spdlog::error("pbr: {}: unknown key {}", kToneMapping, key);
    std::string error;
    if (!pbr::ToGrade(tone, state.grade, error))
        spdlog::error("pbr: {}: {}; using the ACES fit without the grade", kToneMapping, error);
}

// The id the presets' locationId lists use: the loaded location, else the sea, the world map or the main menu.
std::string SceneId()
{
    if (const entid_t location = core.GetEntityId("location"))
        if (const auto *entity = static_cast<Entity *>(core.GetEntityPointer(location)))
            if (entity->AttributesPointer)
                if (const char *id = entity->AttributesPointer->GetAttribute("id"))
                    return id;
    if (core.GetEntityId("sea"))
        return "Sea";
    if (core.GetEntityId("worldmap"))
        return "WorldMap";
    return "MainMenu";
}
} // namespace

void PushPbrLighting(float hour)
{
    State &state = Shared();
    if (!state.loaded)
        Load(state);
    const std::string scene = SceneId();
    const scene_data::LightingPreset *preset = state.library.Find(scene);
    if (!preset)
    {
        ReportOnce(state, "nopreset:" + scene, "no lighting preset lists the scene \"" + scene + "\"; PBR objects there are not drawn");
        return;
    }
    render::PbrLighting lighting;
    lighting.scene = scene;
    lighting.grade = state.grade;
    std::string error;
    if (!pbr::BlendedHourConstants(*preset, hour, lighting.hour, error))
    {
        ReportOnce(state, "nohour:" + scene + ":" + std::to_string(int(hour)),
                   "scene \"" + scene + "\" (" + *state.library.FileOf(scene) + "): " + error + "; PBR lighting of that hour kept from before");
        return;
    }
    render::SetPbrLighting(lighting);
}

} // namespace storm::weather
