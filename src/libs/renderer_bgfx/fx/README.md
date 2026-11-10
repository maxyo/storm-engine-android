# Graphics effects package for the bgfx renderer

Optional upgrades on top of the legacy look: post-processing (tone curve, colour grade, bloom, sun shafts,
FXAA, sharpening / dynamic-resolution upscale), a sea surface, an analytic sky with matching fog, and
per-pixel lighting flags for the fixed-function emulator. `QualityPreset::Original` switches everything off
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
| `include/storm/fx/lighting.hpp`, `shaders/include/fx_lighting.sh` | lighting flags for the fixed-function emulator |
| `include/storm/fx/{color_lut,noise_bake,wave_model,water_grid,sky_model,lighting_model}.hpp` | CPU side, no GPU needed (`storm_bgfx_fx_core`) |
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
fx::Settings settings = fx::Settings::FromPreset(fx::QualityPreset::Medium); // any field may change per frame
settings.grade = fx::GradePreset::Caribbean;

sky.Update(weather, time);                       // every frame, also when only the water uses it
if (settings.sky_atmosphere) sky.Submit(scene_view, settings, view, proj);
if (settings.water == fx::WaterQuality::High)
    water.CaptureScene(capture_view, scene_color, scene_depth, w, h, near, far);
water.UpdateField(field);                        // or PrepareMesh() with the engine's own vertex buffer
water.SubmitField(settings, water_frame, shading, sky, sea_state);
post.Submit(settings, post_frame);               // scene colour -> back buffer, at most PostChain::kMaxViews views
```

All colour targets are gamma-encoded RGBA8, like the legacy pipeline; no float render targets are required.
After a lost context call `OnDeviceLost()` on each object; resources are re-created on the next use.
Sampler stages 5 and 6 belong to `fx_atmosphere.sh`, stages 8 and 9 (vertex shader) to the sea height field.
