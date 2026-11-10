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
| `src/components.*`, `src/template.*` | `<c-…>` elements rendered from spec + template + props |
| `src/behaviors.cpp` | press, hold-to-confirm, long-press tooltip, range, stick, lever, sticky rows |
| `src/draw_elements.cpp` | `<ui-ring>` (hold progress), `<ui-paper>` (aged parchment), `<ui-clamp>` (text cut to N lines with an ellipsis) |
| `src/icons.cpp` | `<ui-icon>`: SVG symbols rasterised per size and colour |
| `src/picture_atlas.*` | the game's `pictures.ini` (group + picture → texture + rectangle), read from the game root at run time |
| `src/tx_texture.cpp`, `src/game_image.cpp` | `<ui-game-image>`: pictures decoded from the player's `.tx` files (or by the host's `game_picture_loader`, never both) on a worker thread; the loading placeholder shows until then, a picture that cannot be had is logged with texture, cell and reason and marked `image-missing` |
| `src/ui_host.cpp` | RmlUi set-up, pointer routing, toasts, tooltip, the HUD mount and the touch map |
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

Nothing above `RenderBackend` calls a graphics API. The bundled GL backend needs a
current context supplied by the host and is the bridge until the engine renders through
bgfx. The bgfx backend will implement the same interface next to the bgfx renderer and
draw inside the engine frame as its last view; `Config::backend` takes it. The host always names its backend (`CreateGl3Backend()` or its own): there is no default and no second choice when it fails. The
application's ARCHITECTURE.md describes that plug-in point (view id, order, texture
handles for `.tx` pictures).

## Licence boundary

Game pictures are never bundled: `<ui-game-image>` reads
`<game root>/RESOURCE/Textures/<texture>.tga.tx` at run time. Tests use synthetic
texture data only.
