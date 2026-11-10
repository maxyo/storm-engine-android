// Caribbean Legend model materials (sidecar <model>.json next to the .gm) -> what the PBR programs need:
// the program variant, the texture of every sampler unit and the per-draw constants. CPU only, no GPU.
// See local-testing/cl20/pbr/DESIGN.md (local) and README.md next to this package.
#pragma once

#include "storm/scene_data/scene_data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace storm::pbr
{

// Fragment program variants (shaders/fs_pbr*.sc).
enum class Variant : uint8_t
{
    Standard, // rma + normal map
    Flat,     // rma, no normal map
    Emissive, // rma + normal map + emission texture
    Layer,    // three PBR layers splatted over the base by an RGB mask
    Count
};
const char *VariantName(Variant variant);

// Sampler units of the PBR programs. 0 and 1 are the model's own textures (.gm stage 0 = albedo on UV0,
// stage 1 = ambient occlusion on UV1), bound by the fixed-function texture loop; the rest come from the
// sidecar or the environment. Mirrored in shaders/include/pbr_common.sh.
enum Unit : uint8_t
{
    kUnitAlbedo = 0,
    kUnitAo = 1,
    kUnitRma = 2,
    kUnitNormal = 3,
    kUnitEmission = 4, // Emissive
    kUnitRadiance = 5,
    kUnitBrdf = 6,
    kUnitMask = 7,     // Layer
    kUnitLayerAlbedo = 4, // Layer: r at 4, g at 8, b at 9
    kUnitLayerRma = 10,   // r, g, b at 10..12
    kUnitLayerNormal = 13, // r, g, b at 13..15
    kUnitCount = 16,
};
uint8_t LayerAlbedoUnit(int layer); // 0 r, 1 g, 2 b

struct TextureRef
{
    uint8_t unit = 0;
    std::string key;  // sidecar key it came from, for messages ("rma_texture", "g_normal_texture", ...)
    std::string path; // relative to Resource/Textures, backslashes (scene_data::TexturePath)
};

struct MaterialDesc
{
    Variant variant = Variant::Standard;
    std::vector<TextureRef> textures; // without units 0 and 1 (model textures) and 5, 6 (environment)
    // Constants. roughness / metallic < 0: from the rma texture; >= 0 replaces that channel (sidecar override).
    float roughness = -1.0f, metallic = -1.0f;
    float emission = 0.0f; // emission strength (sidecar "emission", 1 when an emission texture is given)
    float normal_strength = 1.0f;
    std::array<float, 4> tiling{1.0f, 1.0f, 1.0f, 1.0f}; // base, r, g, b (Layer)
    bool alpha = false;      // alpha tested / blended (sidecar "alpha", "alpha_ccw")
    bool needs_white = false; // no rma texture: unit 2 gets the neutral 1x1 white texture (AO 1, overrides set)
};

enum class Resolution : uint8_t
{
    Pbr,      // `material` is valid
    NotPbr,   // the entry declares no PBR inputs (only flags such as animation / fonar): legacy path, no error
    Rejected, // the entry declares PBR inputs that are invalid: `errors` says why; the caller logs them
};

struct ResolveResult
{
    Resolution resolution = Resolution::NotPbr;
    MaterialDesc material;
    std::vector<std::string> errors; // "<key>: <reason>"
};

// Pure: validates the entry's structure and values. Whether the texture files exist is checked by the caller
// when it creates them (a failed creation rejects the object the same way).
ResolveResult ResolveMaterial(const scene_data::ModelMaterial &entry);

} // namespace storm::pbr
