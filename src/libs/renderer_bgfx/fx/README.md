# Graphics effects package for the bgfx renderer

Upgrades on top of the legacy look: post-processing (ambient occlusion, tone curve, colour grades by place and
hour, bloom, sun shafts, lens flare, dither, FXAA, CAS sharpening), improvements of the game's own sea (`SeaDepth`), an analytic sky with matching
fog, contact shadows, and per-pixel lighting for the fixed-function emulator: sun, point and spot lights, sun
shadows, an ambient-occlusion volume of interiors, rim light, linear-light sums, relief from derived normal maps
and aerial perspective. MSAA, alpha to coverage and anisotropic filtering are renderer state: fx decides them
(`RendererStateFor`), the renderer applies them.

Part of the engine build: `renderer_bgfx/CMakeLists.txt` adds this directory and links `storm_bgfx_fx`;
`src/effects` (`effects::Host`) owns the settings, feeds the per-frame inputs from the engine and calls the fx
objects. The package itself depends on bgfx only, takes plain structs instead of engine types, and generates
every texture it needs.

## Presets

The player chooses «Низкая / Средняя / Высокая» (`QualityPreset`, `Settings::FromPreset`); single options are
`fx_*` keys of engine.ini / `STORM_FX_OPTIONS` (`src/effects/effects.cpp`), for development.

- **Низкая**: everything off, the legacy image untouched.
- **Средняя**: faint bloom on the brightest highlights, MSAA 4x + alpha to coverage, dither, contact shadows,
  a gentle Caribbean grade, anisotropy 4, the sea's band limit, per-pixel lighting with 4 point lights and normal
  maps, sun shadows (low), the ambient-occlusion volume of interiors.
- **Высокая**: as Средняя with anisotropy 8, soft sea edges, specular and translucency, 8 point
  lights, soft sun shadows and ambient occlusion.

Where MSAA is unavailable (no multisampled targets, a GPU-family profile, a failed self-test) `ConstrainSettings`
replaces it with FXAA. Implemented but in no preset (options only; `tests/core_check.cpp` keeps them out of the
presets): tone curve, scene grading by place and hour, lens flare, sun shafts, analytic sky and clouds,
hemisphere ambient, linear-light sums, rim light, aerial perspective, CAS sharpening, the sea's sun specular. Wind sway (`fx_wind.sh`)
is not wired into the game. `Shadows::CasterProgram`,
`Sky::BindAerial/BindWind` and the flash lights are used by `tools/fx-preview` only.

## Layout

| Path | Contents |
| --- | --- |
| `include/storm/fx/settings.hpp` | `Settings`, presets «Низкая / Средняя / Высокая», grades «Оригинал / Карибы / Пасмурно», `ConstrainSettings` |
| `include/storm/fx/post_chain.hpp` | `PostChain`: scene colour → target |
| `include/storm/fx/sea_depth.hpp` | `SeaDepth`: the scene distance behind the game's sea, for its soft edges |
| `include/storm/fx/sky.hpp` | `Sky`: sky pass and the atmosphere state shared with fog and lighting |
| `include/storm/fx/lighting.hpp`, `shaders/include/fx_lighting.sh` | lighting flags and uniforms for the fixed-function emulator |
| `include/storm/fx/light_set.hpp` | point / spot lights: selection per draw call, flash lights |
| `include/storm/fx/normal_map.hpp` | normal maps derived from the game's textures (grey "bump" heights, band-passed luminance of colour textures); the path/name rules table that gives each texture its relief class |
| `include/storm/fx/shadows.hpp` | `Shadows`: sun shadow map |
| `include/storm/fx/contact_shadows.hpp` | `ContactShadows`: soft blobs under characters and objects |
| `include/storm/fx/ao_volume.hpp` | ambient-occlusion volume of interiors: CPU bake from the static geometry, disk-cache format (the renderer side: `src/effects/ao_volume.hpp`) |
| `shaders/include/fx_aerial.sh`, `fx_wind.sh`, `fx_coverage.sh` | aerial perspective, wind sway (not wired), alpha to coverage for the emulator |
| `include/storm/fx/capabilities.hpp`, `log.hpp` | what the device supports; error reporting |
| `include/storm/fx/{color_lut,noise_bake,sky_model,lighting_model,shadow_model}.hpp` | CPU side, no GPU needed (`storm_bgfx_fx_core`) |
| `shaders/` | bgfx shader sources; `shaders.cmake` lists every compiled variant |
| `tests/core_check.cpp` | unit checks of the CPU side |
| `THIRD_PARTY.md` | licences |

## Build

Built with the renderer (see above). Shaders are compiled offline with the pinned shaderc into embedded blobs
(`STORM_FX_SHADER_PROFILE`, default `300_es`); the target `storm_fx_shaders_all_profiles` also compiles every
variant for SPIR-V and Metal. Unit checks of the CPU side: `storm_fx_core_check`, run by
`tests/renderer_bgfx/run.py`. Stand-alone preview of every effect: `tools/fx-preview/README.md`.

## Use

The game's path is `src/effects` (`effects::Host`); the API in short:

```cpp
// Once, and whenever the player changes an option: what the device cannot do is taken out explicitly.
const fx::Support support = fx::DetectSupport({.scene_depth = ..., .msaa_depth = ..., .anisotropy = ...}); // logged once
std::vector<std::string> changes;
fx::Settings settings = fx::ConstrainSettings(fx::Settings::FromPreset(fx::QualityPreset::Medium), support, changes);
for (const auto &line : changes) log(line);      // and grey the options out on the settings screen
const fx::RendererState state = fx::RendererStateFor(settings); // MSAA, alpha to coverage, anisotropy

// Per frame.
sky.Update(weather, time, camera_height);        // every frame, also when only the lighting uses it
if (settings.sea_soft_edges)                     // before the game's sea draws; its shader samples Texture()
    sea_depth.Capture(capture_view, scene_depth, w, h, near, far);
contact_shadows.Submit(scene_view, settings);    // after Add() for each character
post_frame.scene_grade = fx::SceneGrade(sky.State(), overcast, interior);
post.Submit(settings, post_frame);               // scene colour (+ depth for occlusion) -> back buffer

// Per lit draw call of the fixed-function emulator (variant compiled with fx::LightingDefines(settings)):
lighting.Apply(fx::ComputeLighting(input, fx::LightingMaterial::Hull(), &sky.State()));
lighting.Apply(fx::PackPointLights(fx::SelectLights(lights, center, radius, fx::PointLightBudget(settings)), view));
shadows.Bind(view);
```

## No silent fallbacks

The package never swaps an effect for a lesser one behind the caller's back. `ConstrainSettings()` is the
only place where a request is lowered, it happens before rendering, and every change comes back as a line of
text. A call that still cannot be honoured (soft sea edges or ambient occlusion
without a depth texture, a missing device feature) is refused or left out and reported once through
`fx/log.hpp` at `LogLevel::Error`; `tools/fx-preview` fails on any such report. Two limits are part of the
design and are reported, not hidden: the per-draw light budget (`Lighting::TakeDroppedLights()`) and sun
shadows fading out as the sun reaches the horizon.

All colour targets are gamma-encoded RGBA8, like the legacy pipeline; no float render targets are required.
After a lost context call `OnDeviceLost()` on each object; resources are re-created on the next use.
Sampler stages 5 and 6 belong to `fx_atmosphere.sh`, stage 15 to the shadow map, stage 14 to the derived normal map and, in the sea's own pixel shader, to `SeaDepth::Texture()` (above the
fixed-function emulator's units).

Normal maps are derived on the device from the player's own textures (`DeriveNormalMap`, deterministic, RG8
with mips). Cache them on the device; never ship or commit them. Which textures get one, and how strong, is the
ordered rules table of `ClassifyTextureName` (stone/brick strong, wood/planks medium, plaster/ground weak; nothing for
faces, cloth and sails, UI, water, glass, foliage cards, sky, decals and unknown names). Colour textures become height
through their luminance minus a Gaussian of `band_sigma` texels, normalised to unit RMS and softly limited, so wide
dark or bright patches make no pits or hills. Cost: 2 bytes per texel plus a third for the mips (a 1024² map 2.7 MB,
512² 0.7 MB; Settings::normal_map_size caps the resident size).
