// Normal maps derived from the game's grey height ("bump") textures for the per-pixel lighting
// (FX_LIGHT_NORMALMAP, fx/normal_map.hpp, FX.md "Normal maps from the game's height textures").
//
// DX9RENDER::TextureLoad reports each texture's file (SetEffectsTextureSource) right after its levels are filled.
// A texture whose name and pixels make it a height map (fx::HeightMapClassifier, every decision logged once) keeps
// a copy of its top level; while kLightingNormalMap is on, the map is derived from it on a texture worker
// (fx::DeriveNormalMap) and kept in the texture cache directory (texture::write_derived_file: same budget and LRU as
// the ETC2 transcodes), uploaded on first use and bound at sampler stage 14 for draws whose stage 1 holds the height
// texture. Textures loaded while the option is off get their map when it is switched on.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <bgfx/bgfx.h>

#include "storm_render_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace storm::fx
{
class Lighting;
}
namespace storm::shim
{
struct TexStore;
}

namespace storm::effects
{

// One height texture's normal map, owned by its TexStore (TexStore::normal_map).
struct NormalMapEntry
{
    enum State : int
    {
        Source,   // `source` holds the height texture's top level; nothing derived yet
        Pending,  // on a texture worker
        Derived,  // `map` is ready for the upload
        Uploaded, // `handle` is valid
        Failed,
    };
    std::atomic<int> state{Source};
    D3DFORMAT format = D3DFMT_UNKNOWN;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> source; // D3D format, released once the worker took it
    fx::NormalMap map;           // written by the worker before `state` becomes Derived
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    bool failure_reported = false;
    ~NormalMapEntry();
};

class NormalMaps
{
  public:
    void configure(bool enabled) { enabled_ = enabled; }
    // Main thread, right after the texture's levels were filled.
    void on_texture_loaded(shim::TexStore &store, const char *path);
    // The uploaded map of a height texture, or invalid: not a height texture, still being derived (relief appears a
    // few frames later), or failed (logged). Starts the derivation of a map not derived yet.
    bgfx::TextureHandle handle(shim::TexStore &store, fx::Lighting &lighting);

  private:
    void derive(const std::shared_ptr<NormalMapEntry> &entry);

    bool enabled_ = false;
    fx::HeightMapClassifier classifier_;
    fx::NormalMapOptions options_{};
    bool format_reported_ = false;
};

} // namespace storm::effects
