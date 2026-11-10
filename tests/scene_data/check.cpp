// scene_data parsers on synthetic JSON (written for this test); optional local corpus run.
#include "storm/scene_data/scene_data.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace storm::scene_data;

namespace
{
int failures = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                  \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

bool contains(const std::vector<std::string> &list, const std::string &value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

void test_sidecar()
{
    const char *json = R"({
      "models": {
        "Crate": {"reflection": 1, "alpha": 0, "rma_texture": "Resource\\Textures\\Props\\Crate_RMA.dds",
                  "normal_texture": "Resource\\Textures\\Props\\Crate_nom.dds", "path_preset": "Props",
                  "metallic": 0.0, "roughness": 0.5, "sparkle": true},
        "Ground": {"layer_material": 1, "base_tiling": 4, "normal_strength": 1.5,
                   "mask_texture": "Resource\\Textures\\ground_mask.dds",
                   "r_albedo_texture": "a.dds", "r_normal_texture": "an.dds", "r_rma_texture": "ar.dds", "r_tiling": 8,
                   "g_albedo_texture": "b.dds", "g_tiling": 2.5, "b_rma_texture": "cr.dds"}
      },
      "walkCutout": {"Radius": 1.0}
    })";
    Report report;
    const ModelSidecar sidecar = ParseModelSidecar(json, report);
    CHECK(report.ok());
    CHECK(sidecar.models.size() == 2);
    const ModelMaterial &crate = sidecar.models.at("Crate");
    CHECK(crate.reflection && !crate.alpha && crate.path_preset == "Props");
    CHECK(TexturePath(crate.rma_texture) == "Props\\Crate_RMA.dds");
    CHECK(crate.metallic && *crate.metallic == 0.0f && crate.roughness && *crate.roughness == 0.5f && !crate.emission);
    const ModelMaterial &ground = sidecar.models.at("Ground");
    CHECK(ground.layer_material && ground.base_tiling == 4 && ground.normal_strength == 1.5f);
    CHECK(ground.layers[0].albedo == "a.dds" && ground.layers[0].rma == "ar.dds" && ground.layers[0].tiling == 8);
    CHECK(ground.layers[1].tiling == 2.5f && ground.layers[2].rma == "cr.dds" && ground.layers[2].tiling == 1);
    CHECK(contains(report.unknown_keys, "$.models.Crate.sparkle"));
    CHECK(contains(report.unknown_keys, "$.walkCutout"));

    Report bad;
    const ModelSidecar wrong = ParseModelSidecar(R"({"models": {"A": {"alpha": 2, "rma_texture": 5}}})", bad);
    CHECK(bad.errors.size() == 2 && wrong.models.size() == 1);
    Report broken;
    ParseModelSidecar("{\"models\": ", broken);
    CHECK(broken.errors.size() == 1);
}

void test_lighting()
{
    const char *json = R"({
      "schemaVersion": 2, "presetName": "Test", "grass_preset": "grass1", "locationId": ["A_town", "B_port"],
      "sun_azimuth": 120, "sun_height": 35.5,
      "good_rays": {"enabled": true, "density": 0.5, "intensity": 1, "length": 2, "quality": 3},
      "hours": {
        "0": {"ambient": {"color": [0.1, 0.2, 0.3], "intensity": 0.5},
              "sun": {"color": [1, 1, 1], "intensity": 0.01},
              "bloom": {"intensity": 2.0, "treshold": 1.0, "soft_treshold": 0.5},
              "emission": 1.0, "exposure": 1.5, "hdr_texture": "resource/lighting/Night.hdr",
              "shadow": {"ao": 0.8},
              "fog": {"color": [0.5, 0.5, 0.6], "density": 0.02},
              "point": {"torch": {"color": [1, 0.5, 0.2], "intensity": 3}, "candle": {"color": [1, 1, 0], "intensity": 1}},
              "sea": {"color": [0, 0.1, 0.2], "params": [1, 2, 3], "sky": {"color": [0.3, 0.3, 0.4]}}},
        "13": {"exposure": 2, "rain": 1},
        "24": {}
      }
    })";
    Report report;
    const LightingPreset preset = ParseLightingPreset(json, report);
    CHECK(report.errors.size() == 1); // hour "24"
    CHECK(contains(report.unknown_keys, "$.hours.13.rain"));
    CHECK(preset.schema_version == 2 && preset.preset_name == "Test" && preset.location_ids.size() == 2);
    CHECK(preset.sun_azimuth && *preset.sun_azimuth == 120 && preset.god_rays && preset.god_rays->quality == 3);
    CHECK(preset.hours[0] && preset.hours[13] && !preset.hours[1]);
    const LightingHour &night = *preset.hours[0];
    CHECK(night.ambient_color[2] == 0.3f && night.ambient_intensity == 0.5f && night.sun_intensity == 0.01f);
    CHECK(night.bloom_intensity == 2 && night.bloom_threshold == 1 && night.bloom_soft_threshold == 0.5f);
    CHECK(night.exposure == 1.5f && night.shadow_ao == 0.8f && night.hdr_texture == "resource/lighting/Night.hdr");
    CHECK(night.fog && night.fog->density == 0.02f);
    CHECK(night.point_lights.size() == 2 && night.point_lights.at("torch").intensity == 3);
    CHECK(night.sea && night.sea->params[2] == 3 && night.sea->sky_color[2] == 0.4f && !night.sea->deep_color);
    CHECK(preset.hours[13]->exposure == 2 && !preset.hours[13]->fog);
}

void test_texture_library_and_effects()
{
    const char *library_json = R"({
      "grass_preset": {"default": {"albedo_texture": "resource\\Textures\\Grass\\G.dds", "rma_texture": "", "normal_texture": ""}},
      "cloud_preset": {"default": {"albedo_texture": "c.dds", "has_ambient": true, "radiance_scale": 0.7,
                                   "twilight_cloud_tint": [1, 0.8, 0.6],
                                   "rain_layer": {"albedo_texture": "r.dds", "premultiplied_alpha": false}}}
    })";
    Report report;
    const TextureLibrary library = ParseTextureLibrary(library_json, report);
    CHECK(report.ok() && report.unknown_keys.empty());
    const TextureLibraryEntry *grass = library.Find("grass_preset", "default");
    CHECK(grass && TexturePath(grass->albedo_texture) == "Grass\\G.dds" && TexturePath(grass->rma_texture).empty());
    const TextureLibraryEntry *cloud = library.Find("cloud_preset", "default");
    CHECK(cloud && cloud->flags.at("has_ambient") && cloud->numbers.at("radiance_scale") == 0.7f);
    CHECK(cloud->colors.at("twilight_cloud_tint")[1] == 0.8f);
    CHECK(cloud->layers.at("rain_layer").albedo_texture == "r.dds" && !cloud->layers.at("rain_layer").flags.at("premultiplied_alpha"));
    CHECK(!library.Find("cloud_preset", "night"));

    Report effects_report;
    const EffectsTable effects = ParseEffectsTable(
        R"({"___Second___": "", "fire": {"file": "Fire.xps", "speed": 1.5}, "___First___": "", "smoke": {"speed": 1}})",
        effects_report);
    CHECK(effects.effects.size() == 1 && effects.effects.at("fire").speed == 1.5f);
    CHECK(effects.sections == (std::vector<std::string>{"___Second___", "___First___"})); // file order
    CHECK(effects_report.errors.size() == 1); // smoke has no file
}

std::string read(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Local-only: every CL JSON data file of the modelled kinds.
int corpus(const std::filesystem::path &resource)
{
    namespace fs = std::filesystem;
    const auto started = std::chrono::steady_clock::now();
    struct Count
    {
        int files = 0, failed = 0, entries = 0;
        std::map<std::string, int> unknown;
    };
    std::map<std::string, Count> counts;
    const auto record = [&](const std::string &kind, const fs::path &path, const Report &report, int entries) {
        Count &count = counts[kind];
        ++count.files;
        count.entries += entries;
        if (!report.ok() && count.failed++ < 3)
            printf("FAIL %s: %s\n", path.c_str(), report.errors.front().c_str());
        for (const std::string &key : report.unknown_keys)
        {
            // collapse per-model names: $.models.<name>.key -> $.models.*.key
            std::string generic = key;
            if (generic.rfind("$.models.", 0) == 0)
                if (const size_t dot = generic.find('.', 9); dot != std::string::npos)
                    generic = "$.models.*" + generic.substr(dot);
            if (generic.rfind("$.hours.", 0) == 0)
                if (const size_t dot = generic.find('.', 8); dot != std::string::npos)
                    generic = "$.hours.*" + generic.substr(dot);
            ++count.unknown[generic];
        }
    };
    for (const auto &entry : fs::recursive_directory_iterator(resource / "Models"))
    {
        const std::string name = entry.path().filename().string();
        // <model>.json only: <model>.gm.maya.json / .gm.walkcut.json are editor data
        if (!entry.is_regular_file() || entry.path().extension() != ".json" || name.find(".gm.") != std::string::npos)
            continue;
        Report report;
        const ModelSidecar sidecar = ParseModelSidecar(read(entry.path()), report);
        record("model sidecar", entry.path(), report, int(sidecar.models.size()));
    }
    for (const auto &entry : fs::recursive_directory_iterator(resource / "Lighting"))
    {
        std::string ext = entry.path().extension().string();
        for (char &c : ext)
            c = char(tolower(static_cast<unsigned char>(c)));
        if (!entry.is_regular_file() || ext != ".json")
            continue;
        Report report;
        const LightingPreset preset = ParseLightingPreset(read(entry.path()), report);
        int hours = 0;
        for (const auto &hour : preset.hours)
            hours += hour.has_value();
        record("lighting preset", entry.path(), report, hours);
    }
    if (fs::exists(resource / "texture_library.json"))
    {
        Report report;
        const TextureLibrary library = ParseTextureLibrary(read(resource / "texture_library.json"), report);
        record("texture library", resource / "texture_library.json", report, int(library.presets.size()));
    }
    if (fs::exists(resource / "Effects/effects.json"))
    {
        Report report;
        const EffectsTable table = ParseEffectsTable(read(resource / "Effects/effects.json"), report);
        record("effects table", resource / "Effects/effects.json", report, int(table.effects.size()));
    }
    int bad = 0;
    for (const auto &[kind, count] : counts)
    {
        printf("%-16s files %5d  parsed %5d  failed %3d  entries %6d\n", kind.c_str(), count.files,
               count.files - count.failed, count.failed, count.entries);
        for (const auto &[key, n] : count.unknown)
            printf("    unmodelled key %-40s %6d\n", key.c_str(), n);
        bad += count.failed;
    }
    printf("corpus %s: %.1f s\n", resource.c_str(),
           std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    return bad ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
    test_sidecar();
    test_lighting();
    test_texture_library_and_effects();
    if (failures)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("scene_data: all checks passed\n");
    return argc > 1 ? corpus(argv[1]) : 0;
}
