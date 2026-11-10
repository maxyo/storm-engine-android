# PBR materials and lighting presets (Caribbean Legend)

Forward PBR for Caribbean Legend models on the bgfx renderer: the per-model sidecar materials
(`Resource/Models/**/<model>.json`: rma / normal / emission textures, layer materials) and the per-location
lighting presets (`Resource/Lighting/*.json`: ambient, sun, exposure, point-light groups, the hour's `.hdr`
environment), tone mapped with the game's own camera grade (`Resource/INI/aces.json`). Used on the Caribbean Legend
profile only; TEHO never reaches it.

| Path | Contents |
| --- | --- |
| `include/storm/pbr/material.hpp` | sidecar entry → program variant, texture per sampler unit, constants; rejections with reasons |
| `include/storm/pbr/environment.hpp` | `.hdr` strip → GGX-prefiltered radiance cube + SH9 irradiance (bimg), BRDF table, preset hour constants, camera grade |
| `include/storm/pbr/renderer.hpp` | programs, environment upload, per-frame / per-draw uniforms and textures (bgfx only) |
| `shaders/` | `vs_pbr`, `fs_pbr` (rma + normal), `fs_pbr_flat`, `fs_pbr_emissive`, `fs_pbr_layer`; body in `include/pbr_shading.sh` |
| `tests/core_check.cpp` | CPU checks (synthetic data) |

The engine side: `include/storm/render/pbr.hpp` (API), `src/pbr/host.*` (shim hook, environment worker + disk cache),
`geometry/src/pbr_materials.*` (sidecar load), `weather/src/pbr_lighting.*` (preset of the scene and hour),
`src/techniques/pbr.fx` (render state of PBR draws).

## Algorithms

Cook-Torrance (GGX, Smith-Schlick, Schlick Fresnel) and split-sum image-based light (Karis 2013); SH9 irradiance
(Ramamoorthi & Hanrahan 2001); tangent frame from screen derivatives (the `.gm` has no tangents); ACES fit (Hill)
after the game's grade (exposure, contrast, saturation, lift / gamma / gain). Output is display-referred,
gamma-encoded RGBA8 like every legacy draw (the fx LDR convention). At most 4 point lights per draw, chosen from the
lights the engine set near the object.

## Sampler units

0 albedo (`.gm` stage 0), 1 ambient occlusion (`.gm` stage 1, uv1), 2 rma, 3 normal, 4 emission (layer: r albedo),
5 radiance cube, 6 BRDF table, 7 layer mask, 8–9 layer g/b albedo, 10–12 layer rma, 13–15 layer normals.

## No silent fallbacks

Invalid sidecar entries and textures that cannot be loaded are logged with model, object, key and reason; that object
keeps its legacy material (the one documented exception). PBR draws before any lighting preset are skipped and
reported. A missing or undecodable environment turns image-based light off for as long as it is in use, reported
once; a new environment is baked on a worker thread and the previous one stays until it is ready (reported).

## Checks

```sh
python3 tests/pbr/run.py --sdl-source <SDL source> --build-dir build/pbr-check            # core + shim checks
python3 tests/pbr/run.py ... --render --fmt-source <fmt source> --out <dir outside the repo> \
    [--game <CL dir> --model object/Globe_01 --preset BaseTown.json --hour 12 [--texture-path 'Characters\' ]]
```

`pbr_render_check` renders on a headless OpenGL ES 3.0 context (EGL; Mesa llvmpipe works:
`EGL_PLATFORM=surfaceless`). Images of game data are local evidence: never commit or publish them.
