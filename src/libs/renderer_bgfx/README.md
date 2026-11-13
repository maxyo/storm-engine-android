# renderer_bgfx

The engine's only renderer: [bgfx](https://github.com/bkaradzic/bgfx), on OpenGL ES 3.0 or on Vulkan,
on Android and on the Linux development host alike (see "Backends").

- `cmake/bgfx_pin.cmake` — the single pin of bgfx, bx, bimg and of bgfx.cmake (its CMake scripts only).
  `cmake/bgfx_upstream.cmake` adds those targets (patched, see `patches/`). Everything that uses
  bgfx, bx or bimg links `storm_bgfx_runtime` (or `storm_bgfx`), never those targets, and never
  fetches bgfx itself. On Android `storm_bgfx_runtime` is `libbgfx.so`: bgfx, bx and bimg whole in
  one shared library next to `libengine.so`, which `libengine.so` and the UI runtime
  (`libstorm_ui.so`, src/libs/ui) both link, so a process has exactly one bgfx
  (`tools/android/verify_engine.py` refuses a `libengine.so` with its own). Elsewhere it is the
  static libraries. Offline builds pass `-DFETCHCONTENT_SOURCE_DIR_STORM_{BGFX,BX,BIMG,BGFX_CMAKE}=<pinned checkout>` (bgfx patched).
  Licences: `third_party/licenses/{bgfx,bx,bimg}-BSD-2-Clause.txt`.
- `src/shim/` — the D3D9-shaped device. Consumer libraries reach the renderer through `VRenderer`
  (src/libs/renderer/include/renderer.h) with renderer ids (textures, render targets, depth surfaces,
  vertex/index buffers, vertex declarations); `Renderer` drives `shim::Device` directly. The device
  and its objects (`Texture2D`, `CubeTexture`, `VolumeTexture`, `Surface`, buffers, declarations,
  shaders) are plain reference-counted classes on bgfx handles with D3D9-style methods; an
  unimplemented path logs a `[storm-bgfx] TRAP`.
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
- `src/texture/` — the DXT textures' GPU copies, made by background workers with an on-device cache. Each texture's
  mip chain is rebuilt from its top level (`core/mips.hpp`):
  - The game's chains mostly stop at 64x64 and were averaged in gamma space. The rebuilt ones go down to 1x1,
    average in linear light weighted by alpha, and keep the alpha-test coverage of cut-outs.
  - On GPUs with S3TC they are encoded as BC in the texture's own DXT format: libsquish via bimg_encode, with the
    game's top level kept as it is. Otherwise they are ETC2: etcpak via bimg_encode, and bimg's BC decoder.
  - The game's own levels show until the rebuilt texture is ready.
  - The tier (bc / etc2 / rgba8; rgba8 keeps the game's chains) is fixed per GPU at startup and logged once.
    `texture_compression` and `texture_cache_mb` in engine.ini override it. Tests and the benchmark build with
  `-DSTORM_BGFX_BUILD_TESTS=ON` (`storm_texture_tests`, `storm_texture_bench`).
- `patches/` — fixes applied to the pinned bgfx in each build directory's patched view of its shared source copy
  (`bgfx_pin.cmake`, `storm_patched_view`); the configure fails when a patch does not apply or changes nothing.
  `bgfx-anisotropy-level.patch`: the GL backend applies one anisotropy level to every sampler, the device
  maximum; the patch caps it with `bgfx::gl::g_stormMaxAnisotropy` so the presets get 4x / 8x (set by
  `src/effects`). Upstream bgfx has no per-level API; keep it until it does.
  `bgfx-gl-uniform-commit.patch`: the GL backend re-uploaded every uniform of a program whenever one uniform of a
  draw or the program changed; each GL program now keeps a copy of the values it holds and gets only the ones that
  differ (render thread only). Its counters `bgfx::gl::g_stormUniformUploadCalls/Bytes` are the `gl_uniforms` /
  `gl_uniform_kb` of the render metrics; checked by `tests/renderer_bgfx/uniform_commit_check`. Upstream's
  `BGFX_GL_CONFIG_UNIFORM_CACHE` (Emscripten only) is a hash map per register keyed by the reusable GL program id.
  `bgfx-swap-present-hook.patch`: `bgfx::setEglPresent` replaces `eglSwapBuffers` for the window surface of
  the context bgfx created, so the shim presents with Android Frame Pacing (`src/shim/frame_pacing.hpp`,
  Swappy GL from `cmake/swappy.cmake`; tools/android/README.md, "Frame pacing"). Upstream bgfx has no
  present hook; keep it until it does.
  `bgfx-render-thread-hooks.patch`: `bgfx::setRenderFrameDone` reports each frame the render thread has
  finished (rendered and presented), which bgfx has no API for; see "Threads" below.
  `bgfx-shaderc-point-size.patch` (shaderc only): `gl_PointSize` in the SPIR-V build of a vertex shader;
  Vulkan leaves a point's size undefined without it (the stars are a point list).
  `bgfx-vk-present-hook.patch`, `bgfx-vk-unbound-samplers.patch`, `bgfx-vk-builtin-border-colors.patch`,
  `bgfx-vulkan-pipeline-cache.patch`: the Vulkan backend, see "Backends".

## Backends

bgfx is built with two backends, OpenGL ES 3.0 and Vulkan; a device runs one, chosen when it is created
(`src/shim/backend_select.hpp`, `Context::start`):

- Asked for with `render_backend = auto | gles | vulkan` in engine.ini (debuggable Android builds: the graphics
  setting of the same name, 0 / 1 / 2). `auto`, the default, takes Vulkan only for GPU vendors listed in
  `VulkanByDefault` (filled from measurements on real devices; empty so far) and OpenGL ES otherwise.
- Vulkan needs: the system's loader and a device with Vulkan 1.1, `VK_KHR_swapchain` and `VK_KHR_maintenance1`
  (read by `ProbeVulkan` before bgfx is touched); no marker of a Vulkan start on this device and driver that
  never presented a frame (`<cache>/RenderBackend/vulkan-start`, written before `bgfx::init`, removed after three
  presented frames: a driver that kills the process in its start is not started on Vulkan twice); `bgfx::init`;
  and the backend self-test (a known picture drawn into a target and read back).
- A Vulkan that fails any of the last three makes the device start again on OpenGL ES: the one deliberate
  fallback of the renderer, said as an error with the reason. Every start logs one line, which the
  application's diagnostics export keeps:
  `render backend: Vulkan (render_backend = vulkan): <reason>; Vulkan device: <name, version, driver, ids>`.
- OpenGL ES is not a spare: it is the backend of every device Vulkan is not chosen for, and of the launcher's
  screens (their `GLSurfaceView` owns the context).

What differs between the two, all inside the shim:

| | OpenGL ES | Vulkan |
|---|---|---|
| Shaders | ESSL blob | SPIR-V blob (`storm/render/shader_blob.hpp`) |
| Present | Swappy GL (`bgfx-swap-present-hook.patch`) | Swappy Vk (`bgfx-vk-present-hook.patch`), FIFO swap chain |
| Device facts (`shim::DeviceInfo`) | `glGetString` on the render thread | the probe |
| Program cache entries | linked program binaries | the device's one `VkPipelineCache` as a single entry, which every pipeline is created with (`bgfx-vulkan-pipeline-cache.patch`). A scene draw whose pipeline does not exist yet is left out of the frame while a pipeline thread creates it (state bit `STORM_STATE_PIPELINE_MAY_WAIT`, set in `Device::draw` for the scene and for targets rendered frame after frame; `vulkan_async_pipelines = 0` in engine.ini turns it off for measurements); everything else, and every read-back frame, creates its pipelines on the render thread. Creations over 16 ms are logged (`pipeline: N ms on / off the render thread ...`) |
| GPU timers | `EXT_disjoint_timer_query` (`bgfx-gles-timer-queries.patch`) | bgfx's timestamp queries |
| A sampler a draw sets no texture for | samples black | gets a black texture of its kind (`bgfx-vk-unbound-samplers.patch`) |
| `D3DTADDRESS_BORDER` | with a border-clamp extension | always; colours other than transparent / opaque black and opaque white need `VK_EXT_custom_border_color` (`bgfx-vk-builtin-border-colors.patch`; reported when one is used) |
| Multisampled targets | resolved when left; the samples are discarded after it (`bgfx-gl-framebuffer-discard.patch`) | resolved by bgfx after the render pass from the stored samples: no discard of what it reads (`Device::scene_complete`, the UI's layers) |
| Uniforms | only changed ones uploaded (`bgfx-gl-uniform-commit.patch`, `gl_uniforms`) | one uniform block per draw; the counters are 0 |
| Anisotropy level | `bgfx::gl::g_stormMaxAnisotropy` | `bgfx::vk::g_stormMaxAnisotropy` (both `bgfx-anisotropy-level.patch`) |

Orientation and depth range come from bgfx's caps on both (`originBottomLeft`, `homogeneousDepth`).
`tests/renderer_bgfx/run.py` runs every check that draws on OpenGL ES and again on Vulkan
(`STORM_CHECK_BACKEND=vulkan`) where the host has a Vulkan device.

## Threads

bgfx runs multithreaded (`BGFX_CONFIG_MULTITHREADED`). The game thread records a frame; `bgfx::frame()`
(only in `Context::run_frame`) hands it to bgfx's render thread, which owns the EGL context (OpenGL ES) or
records and submits the command buffers (Vulkan), executes the backend and presents (Swappy), after
`bgfx::frame()` has returned. Rules that follow:

- No GL or EGL call outside bgfx. What the driver says about itself is read once on the render thread:
  `shim::Context::gl_info()`.
- `bgfx::makeRef` without a release function only for data that lives as long as the program; otherwise
  `bgfx::copy` or a release callback (it runs on the render thread). A pointer given to `bgfx::read` stays
  valid until `bgfx::frame()` has returned the frame number `bgfx::read` gave.
- bgfx's callbacks (program cache, traces, profiler scopes) and the present function run on the render thread.
- The surface is given back to Android (`request_surface_release`, `surface_lost`) only when the render
  thread has finished every frame handed over. `BGFX_FRAME_FLUSH` frames (read-backs) present nothing and
  are never refused.
- Pacing (`src/shim/frame_overlap.hpp`): after handing a frame over the game thread waits for that frame's
  present, so input reaches the picture as fast as without a render thread; only while game work + render
  work do not fit the frame it waits for the previous frame's present instead and simulates one frame ahead
  (one frame more of latency, each thread a whole frame of time). Logged at each change:
  `render thread: frames take turns` / `frames overlap`.
- `RenderMetrics`: `submit_ms` is the backend's time per frame on the render thread, `wait_ms` the game
  thread's wait for it; the game thread's own work is the frame time minus `wait_ms`.
- Developer keys in engine.ini, read when the renderer starts (debuggable Android builds: graphics settings
  of the same names): `render_thread = 0` keeps bgfx on the game thread (`bgfx::renderFrame()` before
  `bgfx::init`, bgfx's own switch; the launcher's screens under a `GLSurfaceView` always run that way),
  `render_overlap = 0 / 1` forces the pacing choice. For measurements; the start log says which is active
  (`render thread: on` / `OFF`).
- Checked by `tests/renderer_bgfx/render_thread_check` and `tests/frame_pacing`.
- `fx/` — optional effects package (see its README), driven by `src/effects/` (presets, post chain,
  lighting and alpha to coverage of the fixed-function emulator).
  A preset change on a live device (and the device's start, which begins on «Низкая») is staged by
  `effects::Host::advance_switch`: the current settings keep drawing while the new ones' programs are
  linked one per frame and their GPU self-test runs (once per settings set); then they switch at a Present
  and the shim rebuilds only the scene targets whose MSAA / depth read changed. Checked by
  `tests/renderer_bgfx/preset_switch_check`.

## Building

Host and Android builds compile the embedded shaders with the host `shaderc` of the pin, which the
configure step builds once per machine into `~/.cache/storm-deps/shaderc-<key>`
(`cmake/storm_shaderc.cmake`; `-DSTORM_SHADERC=<binary>` overrides it). Every shaderc run goes through
`cmake/shaderc_cache.py`, a content-addressed result cache shared by all build directories and worktrees
(`~/.cache/storm-deps/shader-cache`, `STORM_SHADER_CACHE` moves it): a variant whose shaderc, arguments,
source, varying definition and included files (shaderc `--depends`) are unchanged is copied, not compiled.
A fresh build directory then compiles no shader somebody already compiled (measured 2026-11-14: ~240 ->
~23 CPU-s for the 278 runs of an Android build). `STORM_NO_SHADER_CACHE="<reason>"` turns it off, printed
on every call; hits and misses: `<build dir>/shader-cache-stats.log`. Host:

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
`shaders/ports/*.sc` with `shaderc` at build time, each variant twice: for OpenGL ES (the `300_es`
profile, `STORM_BGFX_SHADER_PROFILE`) and for Vulkan (`spirv`), and embeds the blobs (`--bin2c`,
`<name>` and `<name>_spv`) into the `storm_bgfx_shaders` library; the shim looks them up by name and
takes the blob of the running backend (`storm/render/shader_blob.hpp`; the effects package and the UI
do the same with theirs). Nothing is compiled at run time and no shader files ship in the APK.

The SPIR-V build compiles the sources as HLSL: stage inputs and outputs (`v_*`, `a_*`, `i_data*`,
`gl_Position`, `gl_FragCoord` ...) exist in `main()` only, and a global that is written is not possible.
A helper function gets what it needs as parameters or is a macro expanded in `main()`
(`shaders/include/storm_common.sh`). `cmake/shader_precision_check.py` fails the build on anything
that is not highp, in either blob.

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
