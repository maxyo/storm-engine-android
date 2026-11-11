# Portable game UI runtime

`storm_ui` draws the game interface from a *UI pack* supplied by the application
(documents, styles, component specs, icons, fonts). It is built on RmlUi and is the same
code on every platform; the platform host only owns the window, the frame loop and
touch delivery. The pack and the design rules live in the application repository
(`mobile-ui/design`, see its README and ARCHITECTURE.md).

Build option `STORM_ENABLE_UI` (off in the main engine build; the directory is also a
stand-alone CMake project). Dependencies are SHA256-pinned release archives fetched at
configure time: RmlUi 6.3 (MIT), FreeType 2.14.3 (FTL), lunasvg 3.5.0 with plutovg
(MIT), nlohmann/json 3.12.0 (MIT). Local fixes on top of the RmlUi pin live in `patches/rmlui-*.patch`.
No game data is involved in building or testing.

## Build and check on the host

```sh
cmake -S src/libs/ui -B build/ui-host -G Ninja
cmake --build build/ui-host
ctest --test-dir build/ui-host --output-on-failure       # template language, .tx decoder, screen routing
build/ui-host/storm_ui_shot --pack <pack dir> --doc gallery/button.rml --out shot.png
```

The build pulls in `src/libs/renderer_bgfx` for bgfx, its shader build and the pinned sources
(offline: `-DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=<checkout>`). `storm_ui_shot` renders
off-screen through the UI's bgfx backend on bgfx's GLES 3.0 renderer (Mesa llvmpipe, headless,
simulated clock), the tool acting as the frame owner with the engine's UI view range (`--views N`)
and decoding game pictures from `--game-root` itself. It can replay touches:
`--script "down 1 660 88; wait 0.5; shot held.png; up 1 660 88"`.
Reported component actions are printed as `action <id> <verb> <value>`; `--safe L,T,R,B`
applies safe-area insets in dp (what `Host::SetSafeArea` receives from the platform:
display cutout and system bars). Component templates read them as `safe.left` … `safe.bottom`.

## Android

The engine's Android build (`tools/android/ci_engine.sh`) makes the UI runtime: `game/libstorm_ui.so`
(target `storm_ui_runtime`) next to `libengine.so` and `libbgfx.so`, and the licence texts of its
dependencies in `<build>/licenses/`. The APK ships the three from that one build:

- `libbgfx.so` (`storm_bgfx_runtime`, `src/libs/renderer_bgfx`): bgfx, bx and bimg whole, the
  only bgfx of a process. `libengine.so` (renderer, effects, textures) and `libstorm_ui.so` both
  link it, so the game's UI draws with the engine's bgfx and nothing of bgfx is in the APK twice.
- `libstorm_ui.so`: `storm_ui`, the bgfx backend, RmlUi, FreeType, lunasvg/plutovg and
  `android/jni_host.cpp`. The game links it (`src/libs/ui_host` in `libengine.so` hosts the UI in
  the engine's frame); the application screens without the engine (launcher, downloads,
  gallery, each in its own process) load it alone: their Java view (`StormUiView`, a
  GLSurfaceView) owns the EGL context, bgfx draws on it (`bgfx::PlatformData::context`) through
  the same backend, `android/jni_host.cpp` playing the frame owner. That JNI surface belongs to
  `org.stormengine.port.StormUi`.

Builds with debug info (the engine's Android builds, `-g`) give the UI runtime and its
third-party code line tables only (`-gline-tables-only`): stack traces keep function, file and
line, and the debug APK, which ships its libraries unstripped, stays small. Debug play APK
(Release native build, 2026-11-11): 59.1 MB before the UI was drawn by the engine; 84.1 MB with
`storm_ui` linked into `libengine.so` at full `-g` (`libengine.so` 214 MB uncompressed); 64.6 MB
now (`libengine.so` 105 MB, `libstorm_ui.so` 31 MB, `libbgfx.so` 8 MB uncompressed).

The stand-alone project of this directory is for host checks and captures only.

## Parts

| File | Role |
|---|---|
| `include/storm_ui/ui.h` | `Host`: load a document, touches in, frames out, actions out, safe-area insets |
| `include/storm_ui/render_backend.h` | the only renderer-specific seam |
| `include/storm_ui/backend_bgfx.h`, `src/backend_bgfx.cpp`, `shaders/` | the render backend (bgfx) and frame client |
| `src/components.*`, `src/template.*` | `<c-…>` elements rendered from spec + template + props |
| `src/behaviors.cpp` | press, hold-to-confirm, long-press tooltip, range, stick, lever, table columns |
| `src/draw_elements.cpp` | `<ui-ring>` (hold progress), `<ui-paper>` (aged parchment), `<ui-clamp>` (text in N lines, stepped down to `min-size` before it is cut with an ellipsis) |
| `src/icons.cpp` | `<ui-icon>`: SVG symbols rasterised per size and colour; `icon-shadow: <colour> <x> <y> <sigma>` bakes a drop shadow into the raster (use it instead of `filter: drop-shadow` on an icon: no offscreen layer per frame) |
| `src/picture_atlas.*` | the game's `pictures.ini` (group + picture → texture + rectangle), read from the game root at run time |
| `src/game_image.cpp` | `<ui-game-image>`: pictures from the renderer (`AcquireGamePicture`); the loading placeholder shows until then, a picture that cannot be had is logged with texture, cell and reason and marked `image-missing` |
| `include/storm_ui/tx_texture.h`, `src/tx_texture.cpp` | `.tx` decoding on the CPU for a frame owner without the engine's texture module (`storm_ui_shot --game-root`) |
| `src/ui_host.cpp` | RmlUi set-up, pointer routing, toasts, tooltip, the HUD mount, the touch map, Prepare/Frame |
| `src/frame_renderer.*` | between RmlUi and the backend: frame counters, the filter-result cache, the frame hash (see "Frame cost") |
| `src/hud/` | the in-game HUD: state consumers, scenes by gameplay mode, engine input (see `src/screens/README.md`, "The HUD") |

Errors are loud, not defaults: an unknown `<c-…>` tag, an attribute a component does not
have, an enum value outside its options, a template or class expression error and an icon
missing from the pack are logged with the element (`[ui 1] Component <c-button>
(c-button#id …): …`) and drawn as the striped `ui-error` marker or a crossed red icon.
The one documented fallback: with no game root and no picture loader, game pictures are
"unavailable" (one info line) and keep their neutral placeholder.

Over the game the platform asks `Host::WantsTouch` at pointer-down and gives the UI only
the pointers that land on it; everything else stays the game's. Inside the UI each pointer
is captured by the nearest interactive component under it.
Drag behaviours (stick, lever, slider) keep their pointer exclusively, so several
controls work at once; a tap that travels more than 10 dp is withdrawn and becomes a
list scroll.

## Renderer

Nothing above `RenderBackend` calls a graphics API. There is one backend, bgfx
(`backend_bgfx.h`, target `storm_ui_bgfx`), passed as the required `Config::backend`:

- the game: inside the engine frame (`src/libs/ui_host`);
- the screens without the engine: on the GLSurfaceView's context (`android/jni_host.cpp`);
- captures: on a headless EGL display (`tools/ui_shot.cpp`).

The screens without the engine had RmlUi's GL3 renderer until 2026-11-11. Measured when it was
replaced (emulator, x86_64 with arm64 translation, 1280x720, five cold launcher starts each):
`libstorm_ui.so` 7 346 848 → 8 052 904 bytes stripped (+706 KB; +276 KB gzip; bgfx has since
moved to the shared `libbgfx.so`, see «Android»); device start
(GL3 backend / `bgfx::init` with its capability probes) 429–615 ms → 1356–1687 ms; host start
(fonts, pack) unchanged at 0.4–1.9 s; activity start (`am start -W`) 1.5–2.4 s both. The extra
second before the launcher's first frame on the emulator is bgfx probing texture formats through
the emulator's GL translation. It was kept for one backend: one renderer to keep correct, one
picture path, captures that draw what the game draws.

It draws through `storm/render/frame_owner.hpp`. It is a `FrameClient`: register
  it with `FrameOwner::AddClient`, `AttachHost(host)`, and it lays out on
  `OnSurfaceChanged`, calls `Host::Frame()` in `OnUiRender` (the frame owner's UI view range, after the
  game picture, native resolution, composited premultiplied over the back buffer), and
  on device loss forgets its handles and lets RmlUi regenerate textures and geometry.
  It follows RmlUi's stock GL3 renderer (not built) pass for pass: 2× MSAA layer stack with a shared
  D24S8 stencil for clip masks, the same gradient, blur, drop-shadow, colour-matrix and
  mask shaders (`shaders/*.sc`, built by `storm_bgfx_embed_shaders`). Each render-target
  switch takes one view, and a scheduler lets passes of different elements share views.
  Layer work that only fills a texture (box-shadow images) runs in its own buffers,
  packed side by side, and may span frames when views are short. Running out of the
  frame owner's 64 views is an error (logged): the effects concerned are not drawn.

Game pictures: `<ui-game-image>` asks the backend (`AcquireGamePicture`), which gets them from
the frame owner's `.tx` loader (straight alpha, sampled by its own shader); a picture the
renderer cannot load keeps the placeholder and is logged as an error. Without a picture source
(the screens without the engine) the neutral placeholder stays.

## Frame cost

The platform host draws only when something changed: it calls `Host::Prepare` after every
call into the UI (a state, a touch, a timer) and asks for a `Frame` only when Prepare says
the frame on the screen is out of date. A state the engine publishes again, or one that
changes nothing visible, costs a Prepare (timers, routing, `Context::Update`) and no
drawing. What counts as a change: host calls that alter the documents (touches, toasts,
props, documents, the HUD mount's own edits), rebuilt or re-laid-out components, ticking
behaviours, RmlUi animations and transitions, game pictures that arrived, re-cut text,
and a texture made in the last frame. `Config::verify_unchanged` (host checks and
`storm_ui_shot`) draws every frame anyway and reports a frame Prepare called unchanged that
draws other commands (`FrameStats::mispredicted`, an error line).

`src/frame_renderer.*` sits between RmlUi and the backend's `Rml::RenderInterface`, so every
backend gets the same savings without code of its own:

- it counts the frame (`Host::LastFrameStats`: CPU of update and render, rebuilds, boxes
  laid out, meshes compiled, draws, clip masks, layers, cached layers, texture uploads);
- it caches filter results: an element with `filter:` (drop-shadow, grayscale, brightness,
  opacity) costs a backend an offscreen layer, a filter pass and a composite every frame.
  The runtime buffers the layer's commands and, when they match a frame before (meshes by
  content, textures, positions, scissor, transform, stencil, filters), draws the saved
  result as one textured quad. A layer composited into an empty region (an element
  scrolled out of a list) is dropped. The look is unchanged: captures before and after are
  pixel-identical (`storm_ui_shot` on every bench scene);
- it hashes the drawing commands of each frame (the verification above).

A backend must implement layers and `SaveLayerAsTexture` (RmlUi's box shadows need them
too); without it a filtered element is not drawn and an error says so.

`storm_ui_shot … --script "wait 1; bench 300 [<channel> <a.json> <b.json>]"` measures
frames as a platform host makes them (GPU finished per frame) and prints the costs; with a
channel the two states alternate every frame. `tests/frame_cost_check.cpp` holds the rules:
a steady HUD needs no frame, a drawn steady frame rebuilds, lays out, compiles and uploads
nothing, an unchanged filtered layer costs the backend no layer. Debuggable Android builds
log the frame costs every 5 s (logcat tag `StormUiFrames`).

## Licence boundary

Game pictures are never bundled: `<ui-game-image>` reads
`<game root>/RESOURCE/Textures/<texture>.tga.tx` at run time. Tests use synthetic
texture data only.
