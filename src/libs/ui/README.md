# Portable game UI runtime

`storm_ui` draws the game interface from a *UI pack* supplied by the application
(documents, styles, component specs, icons, fonts). It is built on RmlUi and is the same
code on every platform; the platform host only owns the window, the frame loop and
touch delivery. The pack and the design rules live in the application repository
(`mobile-ui/design`, see its README and ARCHITECTURE.md).

Build option `STORM_ENABLE_UI` (off in the main engine build; the directory is also a
stand-alone CMake project). Dependencies are SHA256-pinned release archives fetched at
configure time: RmlUi 6.3 (MIT), FreeType 2.14.3 (FTL), lunasvg 3.5.0 with plutovg
(MIT), nlohmann/json 3.12.0 (MIT). No game data is involved in building or testing.

## Build and check on the host

```sh
cmake -S src/libs/ui -B build/ui-host -G Ninja
cmake --build build/ui-host
ctest --test-dir build/ui-host --output-on-failure       # template language, .tx decoder, screen routing
build/ui-host/storm_ui_shot --pack <pack dir> --doc gallery/button.rml --out shot.png
```

`storm_ui_shot` renders a document off-screen (EGL pbuffer, software GL 3.3, simulated
clock) and can replay touches: `--script "down 1 660 88; wait 0.5; shot held.png; up 1 660 88"`.
Reported component actions are printed as `action <id> <verb> <value>`; `--safe L,T,R,B`
applies safe-area insets in dp (what `Host::SetSafeArea` receives from the platform:
display cutout and system bars). Component templates read them as `safe.left` … `safe.bottom`.

With `-DSTORM_UI_BGFX=ON` the build also compiles the bgfx backend (bgfx, its shader
compiler and the pinned sources come from `src/libs/renderer_bgfx`; offline:
`-DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=<checkout>`), and
`storm_ui_shot --backend bgfx [--views 16]` draws through it on bgfx's GLES 3.0 renderer
(Mesa llvmpipe, headless), the tool acting as the frame owner with the engine's UI view
range. Captures of both backends match except for antialiased edges (all pack screens:
PSNR 42-76 dB over a mid-tone background).

## Android

```sh
bash tools/android/ci_ui.sh     # needs the NDK prepared by ci_dependencies.sh
```

produces `<build>/package/libstorm_ui.so` (stripped, 16 KiB pages) and
`<build>/licenses/`. The application packages both. The JNI surface in
`android/jni_host.cpp` belongs to `org.stormengine.port.StormUi`.

## Parts

| File | Role |
|---|---|
| `include/storm_ui/ui.h` | `Host`: load a document, touches in, frames out, actions out, safe-area insets |
| `include/storm_ui/render_backend.h` | the only renderer-specific seam |
| `src/backend_gl3.cpp` | GL backend: RmlUi's stock GL3 renderer (GLES 3.0 / GL 3.3) |
| `include/storm_ui/backend_bgfx.h`, `src/backend_bgfx.cpp`, `shaders/` | bgfx backend and frame client (build option `STORM_UI_BGFX`) |
| `src/components.*`, `src/template.*` | `<c-…>` elements rendered from spec + template + props |
| `src/behaviors.cpp` | press, hold-to-confirm, long-press tooltip, range, stick, lever, sticky rows |
| `src/draw_elements.cpp` | `<ui-ring>` (hold progress), `<ui-paper>` (aged parchment), `<ui-clamp>` (text cut to N lines with an ellipsis) |
| `src/icons.cpp` | `<ui-icon>`: SVG symbols rasterised per size and colour |
| `src/picture_atlas.*` | the game's `pictures.ini` (group + picture → texture + rectangle), read from the game root at run time |
| `src/tx_texture.cpp`, `src/game_image.cpp` | `<ui-game-image>`: pictures decoded from the player's `.tx` files (or by the host's `game_picture_loader`, never both) on a worker thread; the loading placeholder shows until then, a picture that cannot be had is logged with texture, cell and reason and marked `image-missing` |
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

Nothing above `RenderBackend` calls a graphics API. Two backends:

`Config::backend` is required; there is no default and no automatic switch.

- **GL** (`CreateGl3Backend`): needs a current GLES 3.0 / GL 3.3 context from the host. Used by the Android `GLSurfaceView` bridge, the gallery and
  host captures.
- **bgfx** (`BgfxBackend::Create`, passed as `Config::backend`): draws inside the
  engine frame through `storm/render/frame_owner.hpp`. It is a `FrameClient`: register
  it with `FrameOwner::AddClient`, `AttachHost(host)`, and it lays out on
  `OnSurfaceChanged`, calls `Host::Frame()` in `OnUiRender` (the frame owner's UI view range, after the
  game picture, native resolution, composited premultiplied over the back buffer), and
  on device loss forgets its handles and lets RmlUi regenerate textures and geometry.
  It follows RmlUi's GL3 renderer pass for pass: 2× MSAA layer stack with a shared
  D24S8 stencil for clip masks, the same gradient, blur, drop-shadow, colour-matrix and
  mask shaders (`shaders/*.sc`, built by `storm_bgfx_embed_shaders`). Each render-target
  switch takes one view, and a scheduler lets passes of different elements share views.
  Layer work that only fills a texture (box-shadow images) runs in its own buffers,
  packed side by side, and may span frames when views are short. Running out of the
  frame owner's 64 views is an error (logged): the effects concerned are not drawn.

Game pictures: `<ui-game-image>` asks the backend (`AcquireGamePicture`). The bgfx
backend gets them from the frame owner's `.tx` loader (straight alpha, sampled by its own
shader); a picture the renderer cannot load keeps the placeholder and is logged as an
error. The GL backend has no renderer behind it and answers `DecodeOnCpu`: the UI reads
the file itself (`Config::game_picture_loader` or the built-in reader).

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
