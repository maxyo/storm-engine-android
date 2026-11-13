# Relief maps from PBRify

Normal maps and material textures (R the height of the parallax and the cavity, G the gloss) for the colour textures
that get relief (fx `ClassifyTextureName`: stone, wood, building atlases, plaster, ground) are made from the player's
game files with PBRify's normal and height models (`fx/relief_model.hpp`) through onnxruntime, and kept in the relief
directory (`fx/relief_store.hpp`). Nothing derived is shipped. Grey "bump" textures are height maps; the game derives
their maps itself. The owner's choice: PBRify for both maps, its normals as the model gives them (no rescale).

## Where the maps are made

In the game process, lazily (renderer_bgfx `src/effects/relief_generator.hpp`): a texture loaded without its map is
requested; one worker thread (nice 10, onnxruntime on all cores but two, paused by the Android thermal status from
MODERATE up) makes the most recently drawn first, then the rest of the location, then, while relief is on and 30 s
after the last request (not while a location loads), the other relief textures of the game by name (`List()`). A map
that becomes ready is uploaded and its relief fades in over 0.3 s. A complete pass prunes stale maps (other games,
older methods) and leaves `complete.txt` (`MarkComplete`): the next starts skip the pass while it matches. A map's key holds its method (`fx::ReliefMethod`: the method version, the
models' digests `fx::kReliefNormalModelDigest`, `fx::kReliefHeightModelDigest`, `fx::kReliefRoughnessModelDigest`, the height's and the gloss's versions, the class's options with the derivation's version, the map size), so a
change never reuses a map made before it; a pass that starts without a matching mark first prunes the maps of other
methods. The maps are a cache: on Android the relief directory is in the app's cache, so «Очистить кэш» (or the
system evicting it) removes them and the game makes them again lazily; a complete pass whose maps went missing
since is resumed (logged with their count). At start the stores of earlier builds are removed (logged): the files
directory's `relief`, kept across «Очистить кэш» (the app removes the old locations in its caches, GameCache); the
store in use is never removed as an old one. Each texture logs the key and method of its map when it is
looked up (`effects: relief <name> (<class>): map <key> of method <method>`). The models
are loaded at the first job and released after 10 s with no work left. The "effects relief:" log line counts the
textures waiting and the rest still to make; each map logs its stages (`relief <name>: WxH, N ms (read, decode,
models of which inference, mips, material (depth), encode, write)`).

`storm_relief_cli <RESOURCE/Textures> <relief dir> <model dir> [map size] [threads]` makes a whole game at once:
host renders with every map in place (engine.ini `relief_dir = <relief dir>`). A host engine makes maps itself only
with engine.ini `relief_models = <the directory with the two model files, e.g. this model/>`; without it, it logs that
no maps are made.

## Models and runtime

- Models: `model/pbrify-normal-int8.onnx`, `model/pbrify-height-int8.onnx` and `model/pbrify-roughness-int8.onnx`
  (CC0-1.0), int8 versions of Kim2091/PBRify_Remix's 1x-PBRify_NormalV3, 1x-PBRify_Height and 1x-PBRify_RoughnessV2
  made by `model/make_models.py` (`model/README.md`; provenance in `third_party/licenses/PBRify-PROVENANCE.txt`). The
  CMake build checks the files against `fx::kReliefNormalModelDigest`, `fx::kReliefHeightModelDigest` and
  `fx::kReliefRoughnessModelDigest`. Per texture: one decode, then one inference of each
  model on the whole image at the map's size, wrap-padded by `fx::kReliefModelPad` (48 px, beyond the networks'
  receptive field: no seam where the texture tiles). Metal (and other classes without a height) runs the normal model
  only.
- Height (`fx/height_map.hpp`): the height model's output, each atlas region normalised against its own neighbourhood
  (`fx::kHeightRegionTexels`), then its 2 % / 98 % become 0 and 1; the class's depth (`fx::ReliefDepthTexels`) is stored
  with it. Gloss (`fx/gloss_map.hpp`): the roughness model at 256² (`fx::kGlossMapSize`), corrected by
  `fx::GlossOfRoughness` (the height's grooves, dark texels, the metal mask, the class caps), stretched into G. The
  material texture is stored beside the normals in the same file (EAC RG11 with mips) for stone, wood, building and
  plaster textures; the other textures of a gloss class (metal, glass, leather...) get their own gloss map
  (`<key>.gloss`: R the metal mask, G the gloss) from the roughness and the height models at 256², in the same queue.
- Runtime: onnxruntime 1.31.1 (MIT), Microsoft's prebuilt Android AAR (`libonnxruntime.so` of the ABI, C API) and Linux
  x64 release on the host; notices in `third_party/licenses/onnxruntime-*`. The CPU execution provider (MLAS kernels),
  waiting threads sleep (no spinning). The XNNPACK provider aborts inside onnxruntime 1.31.1 while the session is
  created (x86_64 emulator), so it is not used; NNAPI is deprecated.
- Maps: the models run on the texture downscaled to 512 px (`fx::kReliefMapSize`, the owner's choice); the GPU's
  bilinear filter upsamples it over the texture. Высокая uses it whole, Средняя from its second mip. On an x86-64 host
  with 4 threads both models take about 0.4 s for a 512 texture.

## Android

The engine build puts `libonnxruntime.so` (libengine.so links it) and `assets/pbrify-normal-int8.onnx`,
`assets/pbrify-height-int8.onnx`, `assets/pbrify-roughness-int8.onnx` into `<build>/game`. The app packages the library next to libengine.so, the models as
the assets of the same names (the engine reads them through SDL_RWFromFile), and the licence files above
(`PBRify-CC0-1.0.txt`, `PBRify-PROVENANCE.txt`, `onnxruntime-*`) on its licences screen. The relief
directory: the engine's cache root (`fs::GetCachePath()`: the app's `(getExternalCacheDir() ?: getCacheDir())/engine`)
+ `/relief` (engine `Renderer`).
