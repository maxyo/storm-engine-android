# Effekseer on bgfx

Particle effects in the Effekseer format (`.efkefc`, `.efkmodel`, PNG/TGA/DDS textures), used by
Caribbean Legend (`Resource/Effects`). Rendering goes through bgfx only; no Effekseer GL renderer
is built.

## Parts

| Target | What | Source |
| --- | --- | --- |
| `storm_effekseer_runtime` | Effekseer core + `EffekseerRendererCommon` (without its texture loaders) | Effekseer **1.80h** (tag `1808`, commit `b5700082b49bc5f919610880dafddc90211a9a4e`), fetched as an archive with a pinned SHA-256 in `CMakeLists.txt`. Only the runtime sources are compiled; the network server is left out. |
| `storm_efkbgfx` | bgfx renderer for Effekseer + embedded shaders | `efkbgfx/`: vendored from [cloudwu/efkbgfx](https://github.com/cloudwu/efkbgfx) `7d89c3c31a8d8d544217b431f9400b2b50003321` (abandoned upstream), ported here, see below. |
| `storm_effekseer` | engine API `include/storm/render/effekseer.hpp` (+ `effekseer_device.hpp` for the frame owner and tests) | `src/system.cpp`, `src/textures.cpp` |

`storm_bgfx` links `storm_effekseer`; `src/effekseer/draw.cpp` (in `storm_bgfx`) draws into the shim's
current render target. Licences: `third_party/licenses/Effekseer-MIT.txt`, `efkbgfx-MIT.txt`.

Sources are fetched once per machine by `storm_fetch_declare`; offline builds pass
`-DFETCHCONTENT_SOURCE_DIR_STORM_EFFEKSEER=<extracted archive of that commit>`.

## efkbgfx port (relative to upstream 7d89c3c)

- Effekseer 1.80: `RenderStateBase` members (`next_`/`active_`), the model vertex has `UV2`
  (layout gets `TEXCOORD1`), sprite lit/distortion shaders apply the tangent handedness
  (`Tangent.w`), `bgfx_create_texture_2d` takes the external handle argument.
- Shaders are compiled at build time for GLES 3.0 by `storm_bgfx_embed_shaders` (one group per
  varying file, renamed to `vs_efk_*`/`fs_efk_*`) and looked up in `renderer/shaders.cpp`, replacing
  upstream's `shader_load` callback. GLSL fixes: no HLSL `(T)0` struct casts, sampler parameters are
  `sampler2D`. Instancing arrays are 10 wide (`MaxInstanced`, as Effekseer's GL renderer on GLES)
  instead of a 40/20 mismatch that over-read the constant buffer.
- Models use Effekseer's loader through the engine's file reader. Textures come from the host
  (`CreateTextureFromHandle`, `ReplaceTextureHandle`); the renderer's own `CreateTexture` only makes
  Effekseer's 1x1 proxy textures. Upstream's texture callbacks and material (`.efkmat`) code are
  removed: CL ships no materials and efkbgfx has no material shader generator; such nodes are
  reported and not drawn.
- No scene background or depth copy is provided: distortion nodes are skipped by Effekseer and
  soft particles are drawn hard. `GetEffectInfo` reports both per effect (CL uses neither).
- `mirror_winding`: the game draws GL render targets upside down (flipped projection), so culling
  swaps CW/CCW like the shim does. Draw-call counters are filled for `GetStats()`.

## Textures

Effect textures (PNG in CL, atlases up to 3.4 MB) go through the engine texture pipeline
(`src/texture/core`, the service behind `texture_compression`/`texture_cache_mb`):

- The cache key is the image file's contents plus its size from the PNG header, so a cache hit
  needs neither a decode nor mip building: the prebuilt ETC2 mip chain (KTX) is uploaded directly.
- On a miss the image is decoded once by bimg, its mips are built (`build_rgba_mips`) and shown as
  RGBA8 at once; the service transcodes them to ETC2 on its workers, writes the disk cache and the
  texture's bgfx handle is swapped in `Update()`. `texture_compression = rgba` keeps RGBA8.
- Effekseer's resource manager shares a texture between the effects that reference the same path.
- `GetStats()` reports cache hits, decodes and transcodes still running.

## Frame integration

- Lifetime: `Context::init` calls `AttachDevice` after `bgfx::init`, `Context::shutdown` calls
  `DetachDevice` before `bgfx::shutdown`. The manager, renderer and shaders are created lazily on
  the first `LoadEffect`, so games without effects pay nothing. Resources survive Android surface
  loss (bgfx keeps the context); `Draw` does nothing while the surface is gone.
- `Draw(device, view, projection)`: opens a fresh game view on the current render target and
  viewport (`Device::begin_external_pass`), after the opaque scene submitted so far and before the
  interface. Effects blend into the gamma-encoded RGBA8 scene target like the game's own particles
  and go through the fx post chain with the rest of the scene.
- Projection: the scene's D3D projection followed by the backend fix-up (Y flip for bottom-left
  render targets, z 0..1 to -1..1 for homogeneous depth). No D3D9 half-pixel offset.
- Time: `Update(seconds)`, Effekseer ticks at 60 per second.
- Sounds referenced by effects are not played yet (no Effekseer sound backend); reported per effect.

## Checks

Built with `-DSTORM_BGFX_BUILD_TESTS=ON`; CTest entries appear with `-DSTORM_CL_GAME_DIR=<game>`
and SKIP (exit 77) when that directory is missing. Game files and pictures stay outside the repo.
`STORM_TEXTURE_CACHE_DIR` selects the texture cache (CTest: `<build>/texture-cache`); `load` prints
the CPU time of every load and waits for the transcodes.

```sh
storm_effekseer_check load   <game dir>                       # every Resource/Effects/*.efkefc, Noop backend
storm_effekseer_check render <game dir> CLTorchFire.efkefc out.png [camera distance]   # headless GLES
```

`render` needs EGL with a pbuffer-capable config (Mesa works without a display).
