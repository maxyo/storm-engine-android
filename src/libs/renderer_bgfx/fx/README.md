# Graphics effects package for the bgfx renderer

Optional upgrades on top of the legacy look: post-processing (ambient occlusion, tone curve, colour grades
by place and hour, bloom, sun shafts, lens flare, dither, optional vignette and grain, FXAA, sharpening /
dynamic-resolution upscale), a sea surface, an analytic sky with matching fog, contact shadows, and
per-pixel lighting for the fixed-function emulator: sun, point and spot lights, flash lights for shots and
explosions, sun shadows, rim light, linear-light sums, relief from derived normal maps, aerial perspective
and wind sway. MSAA, alpha to coverage and anisotropic filtering are renderer state: fx decides them
(`RendererStateFor`), the renderer applies them. `QualityPreset::Low` switches everything off
and leaves the legacy image untouched.

The package is self-contained: it depends on bgfx only, takes plain structs instead of engine types, and
generates every texture it needs. It is not part of the engine build until the renderer adds it.

## Layout

| Path | Contents |
| --- | --- |
| `include/storm/fx/settings.hpp` | `Settings`, presets «Оригинал / Среднее / Высокое», grades «Оригинал / Карибы / Пасмурно» |
| `include/storm/fx/post_chain.hpp` | `PostChain`: scene colour → target |
| `include/storm/fx/water.hpp` | `Water`: sea surface from the engine mesh, the engine height field or Gerstner waves |
| `include/storm/fx/sky.hpp` | `Sky`: sky pass and the atmosphere state shared with water and fog |
| `include/storm/fx/lighting.hpp`, `shaders/include/fx_lighting.sh` | lighting flags and uniforms for the fixed-function emulator |
| `include/storm/fx/light_set.hpp` | point / spot lights: selection per draw call, flash lights |
| `include/storm/fx/normal_map.hpp` | normal maps derived from the game's height ("bump") textures; the rule that finds those textures |
| `include/storm/fx/shadows.hpp` | `Shadows`: sun shadow map |
| `include/storm/fx/contact_shadows.hpp` | `ContactShadows`: soft blobs under characters and objects |
| `include/storm/fx/ao_volume.hpp` | ambient-occlusion volume of interiors: CPU bake from the static geometry, disk-cache format (the renderer side: `src/effects/ao_volume.hpp`) |
| `shaders/include/fx_aerial.sh`, `fx_wind.sh`, `fx_coverage.sh` | aerial perspective, wind sway, alpha to coverage for the emulator |
| `include/storm/fx/capabilities.hpp`, `log.hpp` | what the device supports; error reporting |
| `include/storm/fx/{color_lut,noise_bake,wave_model,water_grid,sky_model,lighting_model,shadow_model}.hpp` | CPU side, no GPU needed (`storm_bgfx_fx_core`) |
| `shaders/` | bgfx shader sources; `shaders.cmake` lists every compiled variant |
| `tests/core_check.cpp` | unit checks of the CPU side |
| `THIRD_PARTY.md` | licences |

## Build

From the renderer, after the bgfx targets exist:

```cmake
set(STORM_FX_SHADERC_COMMAND "${STORM_SHADERC_COMMAND}")
set(STORM_FX_SHADERC_DEPENDS "${STORM_SHADERC_DEPENDS}")
set(STORM_FX_BGFX_SHADER_INCLUDE "${STORM_BGFX_SOURCE_DIR}/bgfx/src")
add_subdirectory(fx)
target_link_libraries(storm_bgfx PRIVATE storm_bgfx_fx)
```

Shaders are compiled offline with the pinned shaderc into embedded blobs (`STORM_FX_SHADER_PROFILE`, default
`300_es`). The target `storm_fx_shaders_all_profiles` also compiles every variant for SPIR-V and Metal.

Stand-alone (host preview, unit checks, all shader profiles): see `tools/fx-preview/README.md`.

## Use

```cpp
// Once, and whenever the player changes an option: what the device cannot do is taken out explicitly.
const fx::Support support = fx::DetectSupport({.scene_depth = ..., .msaa_depth = ..., .anisotropy = ...}); // logged once
std::vector<std::string> changes;
fx::Settings settings = fx::ConstrainSettings(fx::Settings::FromPreset(fx::QualityPreset::Medium), support, changes);
for (const auto &line : changes) log(line);      // and grey the options out on the settings screen
settings.grade = fx::GradePreset::Caribbean;
const fx::RendererState state = fx::RendererStateFor(settings); // MSAA, alpha to coverage, anisotropy

// Per frame.
if (shadows.Begin(settings, shadow_frame))       // then submit casters with shadows.CasterProgram()
    ...

sky.Update(weather, time, camera_height);        // every frame, also when only the water uses it
if (settings.sky_atmosphere) sky.Submit(scene_view, settings, view, proj);
if (settings.water == fx::WaterQuality::High)
    water.CaptureScene(capture_view, scene_color, scene_depth, w, h, near, far);
water.UpdateField(field);                        // or PrepareMesh() with the engine's own vertex buffer
water.SubmitField(settings, water_frame, shading, sky, sea_state);
contact_shadows.Submit(scene_view, settings);    // after Add() for each character / object
post_frame.scene_grade = fx::SceneGrade(sky.State(), overcast, interior);
post.Submit(settings, post_frame);               // scene colour (+ depth for occlusion) -> back buffer

// Per lit draw call of the fixed-function emulator (variant compiled with fx::LightingDefines(settings)):
lighting.Apply(fx::ComputeLighting(input, fx::LightingMaterial::Hull(), &sky.State()));
lighting.Apply(fx::PackPointLights(fx::SelectLights(lights, center, radius, fx::PointLightBudget(settings)), view));
shadows.Bind(view);
sky.BindAerial(view); sky.BindWind();            // FX_AERIAL / FX_WIND variants
```

## No silent fallbacks

The package never swaps an effect for a lesser one behind the caller's back. `ConstrainSettings()` is the
only place where a request is lowered, it happens before rendering, and every change comes back as a line of
text. A call that still cannot be honoured (high water quality without `CaptureScene()`, ambient occlusion
without a depth texture, a missing device feature) is refused or left out and reported once through
`fx/log.hpp` at `LogLevel::Error`; `tools/fx-preview` fails on any such report. Two limits are part of the
design and are reported, not hidden: the per-draw light budget (`Lighting::TakeDroppedLights()`) and sun
shadows fading out as the sun reaches the horizon.

All colour targets are gamma-encoded RGBA8, like the legacy pipeline; no float render targets are required.
After a lost context call `OnDeviceLost()` on each object; resources are re-created on the next use.
Sampler stages 5 and 6 belong to `fx_atmosphere.sh`, stages 8 and 9 (vertex shader) to the sea height field,
stage 15 to the shadow map, stage 14 to the derived normal map (above the fixed-function emulator's units).

Normal maps are derived on the device from the player's own textures (`DeriveNormalMap`, deterministic, RG8
with mips). Cache them on the device; never ship or commit them.
