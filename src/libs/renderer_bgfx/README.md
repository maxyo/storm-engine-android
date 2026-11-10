# renderer_bgfx

The engine's only GPU backend: [bgfx](https://github.com/bkaradzic/bgfx) on OpenGL ES 3.0,
on Android and on the Linux development host alike.

- `cmake/bgfx_pin.cmake` — the single pin of bgfx.cmake (bgfx/bx/bimg as its submodules).
  Other libraries link the `storm_bgfx` target and never fetch bgfx themselves. Offline
  builds pass `-DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=<pinned checkout>`.
  Licences: `third_party/licenses/{bgfx,bx,bimg}-BSD-2-Clause.txt`.
- `src/shim/` — the D3D9-shaped shim. `DX9RENDER` (src/libs/renderer) and its consumers still
  call `IDirect3DDevice9` and friends, declared by the vendored type headers in
  `third_party/d3d9-headers/`; the objects behind those interfaces are implemented here on bgfx
  handles. `d3d9_stubs.gen.hpp` makes every unimplemented method log a `[storm-bgfx] TRAP`.
- `shaders/` — fixed-function emulation (`vs_ff`, `fs_ff*`), the blit used for compositing,
  and `include/` with the shared conventions (`storm_common.sh`). The graphics effects add
  `vs_ff_lit`, `fs_ff_lit*`, `fs_ff_baked*` and `fs_ff_coverage` (per-pixel lighting, point lights,
  alpha to coverage; variant list in `CMakeLists.txt`, selection in
  `src/effects/lighting.hpp`); the «Оригинал» preset never uses them.
- `shaders/ports/` + `src/shim/ports.cpp` — hand ports of the 15 vs_1_1/ps_1_x shaders used by
  `src/techniques`. `CTechnique` passes the shim a reference blob with the technique shader key
  (`<fx base name>_<shader name>`, e.g. `sea_sea2_vertex_shader`); unknown keys are logged and
  their draws skipped.
- `include/storm/render/frame_owner.hpp` — the frame owner contract (bgfx context, surface,
  view order, client views for the UI). The header is the documentation.
- `host-tools/` — builds only the host `shaderc` from the same pin (`cmake/storm_shaderc.cmake` builds it
  once per machine and pin for every host and cross build).
- `src/texture/` — DXT → ETC2 transcode (etcpak, pinned in this CMakeLists) with an on-device cache for
  GPUs without S3TC. The tier (bc / etc2 / rgba8) is fixed per GPU at startup and logged once;
  `texture_compression` and `texture_cache_mb` in engine.ini override it. Tests and the benchmark build with
  `-DSTORM_BGFX_BUILD_TESTS=ON` (`storm_texture_tests`, `storm_texture_bench`).
- `patches/` — fixes applied to the pinned bgfx when its shared source copy is fetched (`bgfx_pin.cmake`) (currently: re-attach to a new
  Android native window after the activity returns from background). The same fix is upstream as bgfx
  99b7977f ("Android: restore main-window EGL surface recreation", the commit right after the pinned
  fd541f05). It is not taken yet because every bgfx.cmake revision that carries it (5730f7a and later)
  replaces the bgfx/bx/bimg submodules with FetchContent, which changes how this pin, host-tools/ and
  the offline Android sources work. Drop the patch with that pin bump.
- `fx/` — optional effects package (see its README), driven by `src/effects/` (presets, post chain,
  lighting and alpha to coverage of the fixed-function emulator).

## Building

Host and Android builds compile the embedded shaders with the host `shaderc` of the pin, which the
configure step builds once per machine into `~/.cache/storm-deps/shaderc-<digest>`
(`cmake/storm_shaderc.cmake`; `-DSTORM_SHADERC=<binary>` overrides it). Host:

```sh
cmake -S . -B build/host -G Ninja -DSTORM_HOST_SOURCES=<prepared sources> -DSTORM_AUDIO_SOURCES=<audio sources>
ninja -C build/host engine
```

Android:

```sh
cmake -S . -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/android-arm64.cmake \
  -DSTORM_ANDROID_NDK=... -DSTORM_ANDROID_DEPS=... -DSTORM_ANDROID_SOURCES=... -DSTORM_AUDIO_SOURCES=...
ninja -C build/android engine
```

`tools/android/ci_engine.sh` does this.

## Shaders

`cmake/storm_shaders.cmake` (`storm_bgfx_embed_shaders`) compiles every `shaders/*.sc` and
`shaders/ports/*.sc` with `shaderc` for the `300_es` profile (`STORM_BGFX_SHADER_PROFILE`) at
build time and embeds the blobs (`--bin2c`) into the `storm_bgfx_shaders` library; the shim
looks them up by name. Nothing is compiled at run time and no shader files ship in the APK.

## D3D9 conventions handled by the shim

- Matrices are D3D row-vector matrices and are uploaded as they are; the shaders multiply
  `vector * matrix`.
- A clip-space fix-up matrix is applied last: D3D depth 0..1 is remapped to -1..1 when the
  backend uses homogeneous depth, plus the D3D half-pixel offset and depth bias. vs_1_1 ports
  output D3D clip space and apply the same matrix themselves.
- GL render targets have a bottom-left origin: the fix-up flips Y and culling is mirrored
  (CW and CCW swapped); viewports, scissors and blits account for the flip.
- `D3DCOLOR` vertex colours arrive as B,G,R,A bytes and are swizzled in the shader
  (`D3DCOLOR_TO_RGBA`).
- Triangle fans are converted to triangle lists in transient index buffers.
- Vertex/index buffers that are not written in the current frame are drawn from immutable bgfx
  snapshots, re-created when they go stale; buffers written in the current frame, user-pointer
  draws and fans go through transient buffers.
- Textures are created from an in-memory DDS so that partial mip chains (common in `.tx`
  files) keep their mip range.
- Render-target to texture copies (`StretchRect`, D3DX surface loads) stay on the GPU: the
  destination becomes a render target.

## Known limits

- 5 fixed-function texture stages (`kFfStages`).
- 1 user clip plane, emulated with `discard` in the fragment shader.
- `D3DTEXF_NONE` as a mip filter is not emulated (sampled like `D3DTEXF_POINT`).
- The game may use views `0..223` per frame (`kGameViewEnd`); later passes reuse the last view
  and the overflow is logged once.
