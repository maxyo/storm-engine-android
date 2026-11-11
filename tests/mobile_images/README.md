# Native mobile widget images (schema 2)

The engine owns resource lookup and atlas resolution. The interface model carries opaque
picture handles and their sources, never paths or renderer texture pointers. No commercial
textures belong in this repository or APK.

## Additive snapshot contract

The existing `showMobileWidgets(byte[])` JSON fields remain available unchanged.
Top-level additions are `schemaVersion:2`, `screenRevision` (new interface lifetime,
including reopening the same screen), `revision` (changed serialized content), and
`language` (the current game's `VSTRSERVICE::GetLanguage()` value, e.g. `russian` or
`english`; empty if not initialized). This is authoritative game state, not the
Android locale or a list of available translations. Game labels remain localized
UTF-8 unchanged. App chrome may map known language names to its own translation
catalog, with an explicit fallback; do not infer installed translations from old
language.ini registry entries.

TABLE remains `type:"choice"`, with its old tab-joined `label`, zero-based `index`
and `value:"1"` for selected / `"0"` otherwise. Additions:

- `rowId`: exact `trN.UserData.ID`, or empty when the script supplies no ID.
- `cells`: ordered objects `{column,text,colorArgb,images}`; columns are zero-based.
- `images`: array of `{handle,imageRevision,width,height,tintArgb}` references.
- `actions`: permitted `select` and, only when the node's actual native command
  configuration allows it, `activate`.

Ordinary PICTURE nodes export `type:"image"` and `images`. They retain source path
metadata through INI loads and runtime picture changes. Video and GPU-only sources
are intentionally omitted. `SAVEIMG` retains its old `type:"save"` behavior.
`tintArgb` is separate native vertex tint metadata.

Only enabled actions may be dispatched. Existing ordinary button/picture `press`
uses the existing action path. For a TABLE request use the existing widget-action
transport with `value="select@<screenRevision>:<rowId>"` or
`value="activate@<screenRevision>:<rowId>"`. Keep its node and zero-based index.
Empty row ID means an empty suffix after `:`. Do not parse labels for semantics.
Legacy unversioned `select` remains supported for old apps.

`activate` first selects, then queues an internal continuation. XINTERFACE resolves
the node again after the selection callback, checks screen epoch, row ID/index and
selection, and invokes the configured activate/double-click command. This matters
for Ship cargo: selection alone does not open the quantity window. The app must
not send a separate select followed by an assumed activation. Back/cancel and modal
visibility remain the game's existing native commands.

## Pictures

A picture of the model is a reference: `{handle,imageRevision,width,height,tintArgb}` plus,
where the engine knows it, its `source` (`atlas` group/image, `battle_icon` mode/atlas/pic, or
`texture` with the rectangle shown). Handles are opaque, one per texture path and rectangle,
never reused across screen lifetimes, at most 4096 per screen; paths outside the game's texture
namespace are refused. The engine sends no pixels: the portable UI (src/libs/ui, inside the
engine frame) draws pictures by their source through the renderer's texture module.

## Validation

```sh
python3 tests/mobile_images/run.py --build-dir build/host-ci/build/mobile_images
python3 tests/mobile_images/run.py --build-dir build/mobile-images-clang --compiler clang++ # needs Clang sanitizer runtimes
python3 tests/mobile_images/compile_android.py --reference-build /path/to/existing/engine-build
```

The standalone host test compiles the production registry and schema with ASan/UBSan and checks
stable handles, screen invalidation, rectangle changes, refused paths, the descriptor cap, the
picture sources and the widget JSON. The ARM64 checker reads a Ninja compile command without
building or modifying its reference directory; new objects are compiled in a temporary directory.

If a host Clang installation lacks sanitizer runtimes, `--no-sanitize` allows an
explicit compiler-only second run; retain a separate sanitized GCC/Clang run.
