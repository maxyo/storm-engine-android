// Caribbean Legend PBR materials and lighting presets as seen by the engine (renderer_bgfx/pbr, src/pbr).
// Game thread only. Used on the Caribbean Legend profile only: TEHO never calls any of this.
//
//   * geometry: a CL model's object with a sidecar material holds a PbrMaterial; GEOM_SERVICE_R calls
//     SetPbrMaterial(&m) around the DrawBuffer of that object (with a Pbr* technique) and SetPbrMaterial(nullptr)
//     after it. The shim then draws it with the PBR program instead of the fixed-function emulator.
//   * weather: SetPbrLighting() once per frame with the hour's constants of the current location's preset and its
//     environment map. The environment is baked on a worker thread (bimg GGX filter) and cached on disk.
#pragma once

#include "storm/pbr/environment.hpp"
#include "storm/pbr/material.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct IDirect3DBaseTexture9;

namespace storm::render
{

struct PbrConfig
{
    std::filesystem::path cache_directory; // baked environments (per-user storage); empty: no disk cache
    // Reads a game file (e.g. "resource\\lighting\\Day.hdr") through the engine's file service; empty on failure.
    std::function<std::vector<uint8_t>(const std::string &path)> read_file;
};
void ConfigurePbr(const PbrConfig &config);

// A CL object's material: the resolved description and the textures of its units (index = pbr::Unit;
// units 0 and 1 come from the model's own stages, 5 and 6 from the environment).
struct PbrMaterial
{
    pbr::MaterialDesc desc;
    std::array<IDirect3DBaseTexture9 *, pbr::kUnitCount> textures{};
    std::string name; // "<model>:<object>" for messages
};

// The material of the following draws; nullptr ends it.
void SetPbrMaterial(const PbrMaterial *material);

struct PbrLighting
{
    std::string scene;      // the location / scene id the preset was chosen for (messages)
    pbr::HourLighting hour; // hour.hdr_texture names the environment
    pbr::Grade grade;       // the camera grade (Resource/INI/aces.json)
};
// The lighting of this frame. A frame without the call keeps the last lighting.
void SetPbrLighting(const PbrLighting &lighting);

// Colour x intensity of the current hour's point-light group `group` (a lights.ini light type, any case); false when
// the preset has no such group or no lighting was set.
bool PbrPointLightGroup(const char *group, std::array<float, 3> &rgb);

} // namespace storm::render
