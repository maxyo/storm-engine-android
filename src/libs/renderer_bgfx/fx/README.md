# Graphics effects package for the bgfx renderer

Upgrades on top of the legacy look: post-processing (ambient occlusion, tone curve, colour grades by place and
hour, bloom, sun shafts, lens flare, dither, SMAA, the FSR 1 upscale and sharpening), improvements of the game's own sea (`SeaDepth`), contact shadows, and per-pixel lighting for the fixed-function emulator: sun, point and spot lights, sun
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

Every preset renders the 3D scene at an internal height on taller displays and upscales it with FSR 1 (EASU, then
RCAS sharpening at the display size); the game's interfaces are drawn at the display size after it. A height, not a
factor: a display that is not taller renders natively, so a 720-line phone (and a 1080-line one on Высокая) is not
scaled for nothing.

| Preset | Internal height | 3200x1440 | 2400x1080 | 1600x720 |
| --- | --- | --- | --- | --- |
| Низкая | 900 | 2000x900 (0.63) | 2000x900 (0.83) | native |
| Средняя | 1000 | 2222x1000 (0.69) | 2222x1000 (0.93) | native |
| Высокая | 1100 | 2444x1100 (0.76) | native | native |

The heights never decrease from Низкая to Высокая: a higher preset never renders the scene smaller.

- **Низкая**: the legacy image (no effect) at the internal height above, upscaled by EASU + RCAS only (no other pass).

On every preset the post chain also draws the game's underwater screen wobble (`Render.SeaEffect`,
`PostChain::Frame::wobble`, `fs_fx_wobble.sc`): the legacy look, which the D3D9 engine's dead post-process chain
never showed on this port.
- **Средняя**: faint bloom on the brightest highlights, MSAA 4x + alpha to coverage (on the internal picture,
  before the upscale), dither, contact shadows,
  a gentle Caribbean grade, anisotropy 4, the sea's band limit, per-pixel lighting with 4 point lights and normal
  maps, sun shadows (low), the ambient-occlusion volume of interiors, the AgX tone curve on the luminance
  (strength 0.6), the grade of the place and hour (hue and warmth only), aerial perspective and height fog, rim light,
  sunlight through the sails (a warm term on sails and flags with the sun behind them, scaled by the luminance of the
  canvas texture, the sun's visibility taken on the sail's sunlit side), waves from objects (`fx_sea_waves`,
  `fx_sea_waves_lift`, `fx_sea_wakes`): a wave-equation ripple field of 128 m around the camera stirred by swimmers,
  waders and hulls (`SeaWaves`) in the sea's normals, crest foam and vertices, and a Kelvin wake behind every ship
  under way (`shaders/include/sea_waves.sh`). The lift needs vertex texture fetch (GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS;
  switched off loudly without it); the heights are RG16F where that renders, packed RGBA8 otherwise (logged).
  `fx_sea_waves_debug=1` shows the ripple slope red, the wake slope green, crest foam blue.
- **Высокая**: as Средняя with anisotropy 8, soft sea edges, specular, 8 point
  lights, soft sun shadows and ambient occlusion, the sun glare (replacing the game's sun glow and flare sprites)
  and sun shafts.

Where MSAA is unavailable (no multisampled targets, a GPU-family profile, a failed self-test) `ConstrainSettings`
replaces it with SMAA 1x (logged "MSAA unavailable: SMAA instead"; without the post chain or R8/RG8 textures there
is no anti-aliasing, logged too). MSAA stays the anti-aliasing where it exists: it resolves the sub-pixel rigging,
masts and alpha-tested foliage that a post filter cannot recover, and costs little on tile GPUs.

The internal height (`Settings::render_height`, option `fx_render_height`, `fx::RenderScale()`; never below half
the display, FSR 1's range) applies to 3D frames: the shim
(`src/shim/device.cpp`) draws them into internal-size scene stores while the game keeps addressing display pixels
(viewports, scissors, `GetDesc`), the post chain runs at the internal size (ambient occlusion, bloom, grade, SMAA;
the sea's soft edges and contact shadows draw into the internal scene) and EASU writes the display-size picture the
interfaces then draw on. Frames without 3D (menus, loading screens) are drawn at the display size; the change is
logged as "effects: render scale ...: the 3D scene at WxH (internal height H), upscaled by FSR 1 to WxH" (or "the 3D
scene at the display size"). Without post effects the chain is EASU + RCAS alone. RCAS runs only after an upscale. With `fx_gpu_profile=1` the
profile line gives `scene=`/`display=` sizes and `gpu_smaa_ms`, `gpu_easu_ms`, `gpu_rcas_ms`. `fx_gpu_profile` is a measurement
option, off by default: it times every view with GPU timer queries (BGFX_DEBUG_PROFILER), which costs CPU in the GL
backend (on the x86_64 emulator about 40 ms more GL submit per frame in a town); without it bgfx times only the whole
frame (`gpu_ms` in RenderMetrics). The GPU self-test of
a settings switch runs the chain with an upscale when the settings have an internal height.

Implemented but in no preset (options only): hemisphere ambient,
linear-light sums, the sea's sun specular. Wind sway (`fx_wind.sh`)
is not wired into the game. `Shadows::CasterProgram`,
`Sky::BindAerial/BindWind` and the flash lights are used by `tools/fx-preview` only.

## Layout

| Path | Contents |
| --- | --- |
| `include/storm/fx/settings.hpp` | `Settings`, presets «Низкая / Средняя / Высокая», grades «Оригинал / Карибы / Пасмурно», `ConstrainSettings` |
| `include/storm/fx/post_chain.hpp` | `PostChain`: scene colour → target |
| `include/storm/fx/sea_depth.hpp` | `SeaDepth`: the scene distance behind the game's sea, for its soft edges |
| `include/storm/fx/sea_waves.hpp`, `wave_clock.hpp` | `SeaWaves`: the ripple simulation around the camera (its sea shader side: `shaders/include/sea_waves.sh` of the renderer, with the Kelvin wakes) |
| `include/storm/fx/sky.hpp` | `Sky`: the atmosphere state of the weather (grade of the place and hour, hemisphere ambient, aerial perspective); the sky itself is the game's on every preset |
| `include/storm/fx/lighting.hpp`, `shaders/include/fx_lighting.sh` | lighting flags and uniforms for the fixed-function emulator |
| `include/storm/fx/light_set.hpp` | point / spot lights: selection per draw call, flash lights |
| `include/storm/fx/relief_model.hpp`, `relief_store.hpp` | PBRify's normal and height models' input, crop, axes and mips (run by src/libs/relief); the relief directory's files and keys |
| `include/storm/fx/gloss_map.hpp` | gloss maps derived from the colour textures (material class from the path rules, luminance and colour cues per texel) that shape the sun's and the lamps' highlights |
| `include/storm/fx/height_map.hpp` | the relief's material texture: PBRify's height (normalised per atlas region) for the parallax and the cavity, the gloss beside it |
| `include/storm/fx/normal_map.hpp` | normal maps of the grey "bump" heights; the path/name rules table that gives each texture its relief class |
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

Built with the renderer (see above). Shaders are compiled offline with the pinned shaderc into embedded blobs, each
variant for OpenGL ES (`STORM_FX_SHADER_PROFILE`, default `300_es`) and for Vulkan (SPIR-V); the target
`storm_fx_shaders_all_profiles` also compiles every variant for Metal. Unit checks of the CPU side: `storm_fx_core_check`, run by
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
Stage 15 belongs to the shadow map, stage 14 to the derived normal map and, in the sea's own pixel shader, to `SeaDepth::Texture()` (above the
fixed-function emulator's units).

Normal maps come from the player's own textures, on the device; never ship or commit them. Which textures get one is
the ordered rules table of `ClassifyTextureName` (stone, wood, building atlases, plaster and ground; nothing for faces,
cloth and sails, UI, water, glass, foliage cards, grass ground, sky, decals and unknown names). Colour textures get
PBRify's normals as the model gives them (`relief_model.hpp`, the owner's choice), made by src/libs/relief; grey "bump"
textures get `DeriveNormalMap` of their heights, in the game. Both live in the relief directory (`relief_store.hpp`) as EAC RG11 with
mips: 1 byte per texel plus a third (a 512² map 0.35 MB; prepared maps are 512, fx::kReliefMapSize; Settings::normal_map_size caps the resident
size).

Gloss maps (`gloss_map.hpp`, Settings::gloss, «Средняя» and «Высокая») come from the same textures the same way: the
material class from the path (`ClassifyGlossName`: metal, glass, leather, lacquered wood and wet cave stone glossy;
painted hulls a little; dry wood, stone, plaster and cloth next to nothing; characters none), moved per texel by its
brightness against its surroundings and, in building atlases and on metal, by its colour (plaster, wood, lacquer,
window glass; rust). Derived in the game on a texture worker (~10 ms a texture, no model) into the relief directory
as EAC R11 with mips (256², 43 KB). The lit and baked programs with FX_LIGHT_SPECULAR read them at stage 7
(fx_lighting.sh `fxGlossSpec`: energy-kept Blinn-Phong, power 8..181, reflectance gloss^2.5 / 2): the sun's highlight on
lit and on baked surfaces outdoors (where the bake shows the open sun), the lamps' highlights with their shadows.
The colour textures' maps of the classes with a height (stone, wood, building atlases, plaster) also hold the relief's
material texture (`height_map.hpp`): R PBRify's height for the parallax and the cavity, G the gloss (so these textures
have no gloss map of their own), EAC RG11 with the same mips in the same file (a 512² material 0.35 MB). With Settings::parallax (Высокая: occlusion, 8..24 steps by view angle;
Средняя: one-tap offset mapping) the lit and baked draws whose relief follows stage 0 take the FX_LIGHT_PARALLAX variants:
the material at stage 7 (`kMaterialStage`, `s_fxMaterial`, the unit the gloss reads), stage 0's colour and the
normal map read where the eye's ray meets the relief (`fx_lighting.sh` fxParallax), fading out between 10 and 15 m.
The height is normalised per atlas region (`height_map.hpp`), each class has its
own depth (`ReliefDepthTexels`: stone 12 texels of the 512 map, building atlases 8, plaster 5, wood 4), the shift at grazing views is held to 2.5 times the depth, and clamp-addressed textures have none (the
reads would run off their edge). On Высокая the relief also shadows itself towards the sun (fxParallaxShadow, six taps
of the height; not indoors): the lit draws' sun term, half of the baked colours.
The same height gives the relief's cavity on Высокая and Средняя (fxReliefCavity: the height against itself two and a
half levels coarser, one more tap): joints, gaps and recesses take ambient and bounce in full, half of the direct light,
three quarters of the baked colours; it also draws where the parallax is off or clamped.
The grey "bump" textures' maps (stage 1, their own coordinates) have no height and no parallax.
