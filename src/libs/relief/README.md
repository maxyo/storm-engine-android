# Relief maps from PBRify

Normal maps and material textures (R the height of the parallax and the cavity, G the gloss) for the colour textures
that get relief (fx `ClassifyTextureName`: every surface but characters, sails and flags, rigging, foliage, water,
glass, sky, UI, effects and light maps) are made from the player's
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
with engine.ini `relief_models = <the directory with the three model files>`; without it, it logs that no maps are
made. The build makes that directory (below); its path is in `<build>/relief_models_path.txt` of a host build.

## Models and runtime

- Models: `pbrify-normal-int8.onnx`, `pbrify-height-int8.onnx` and `pbrify-roughness-int8.onnx` (CC0-1.0), int8
  versions of Kim2091/PBRify_Remix's 1x-PBRify_NormalV3, 1x-PBRify_Height and 1x-PBRify_RoughnessV2, not kept in the
  repository: the build fetches the float models by their pins and runs `model/make_models.py` once per machine
  (`model/README.md`; provenance in `third_party/licenses/PBRify-PROVENANCE.txt`). Their digests are pinned in this
  folder's CMakeLists.txt and match `fx::kReliefNormalModelDigest`, `fx::kReliefHeightModelDigest` and
  `fx::kReliefRoughnessModelDigest`. Per texture: one decode, then one inference of each
  model on the whole image at the map's size, wrap-padded by `fx::kReliefModelPad` (24 px, beyond the networks'
  receptive field: no seam where the texture tiles); the roughness model at the gloss map's size.
- Height (`fx/height_map.hpp`): the height model's output, each atlas region normalised against its own neighbourhood
  (`fx::kHeightRegionTexels`), then its 2 % / 98 % become 0 and 1; the class's depth (`fx::ReliefDepthTexels`) is stored
  with it. Gloss (`fx/gloss_map.hpp`): the roughness model at 256² (`fx::kGlossMapSize`), corrected by
  `fx::GlossOfRoughness` (the height's grooves, dark texels, the metal mask, the class caps), stretched into G. The
  material texture is stored beside the normals in the same file (EAC RG11 with mips) for every relief texture; the
  textures of a gloss class without relief (glass...) get their own gloss map
  (`<key>.gloss`: R the metal mask, G the gloss) from the roughness and the height models at 256², in the same queue.
- Devices (`backend.hpp`): the GPU through ncnn 20261020 (BSD-3-Clause; Vulkan compute, its in-house loader opening
  libvulkan at run time; `cmake/ncnn.cmake`; notices in `third_party/licenses/ncnn-*`, `glslang-*`), running ncnn
  graphs of the same float models (fp16 weights, made at build time by pnnx: `model/make_ncnn_models.py`), or the CPU
  through onnxruntime 1.31.1 (MIT; Microsoft's prebuilt Android AAR and Linux x64 release; notices in
  `third_party/licenses/onnxruntime-*`), the int8 models on its CPU provider (MLAS), waiting threads sleeping. The
  device is measured once per game: a probe tile on both (`ChooseDevice`), the faster taken, logged with both numbers
  (`relief device: GPU (measured now on <GPU>, driver <v>: ... GPU x ms ..., CPU y ms ...)`) and kept in the relief
  directory's `relief-device.txt` for that GPU, driver, model and thread count; «Очистить кэш» measures again. Without
  a Vulkan compute device the CPU is taken and the reason logged. On the GPU the image runs in tiles of 128 texels with
  the networks' 24-texel reach around each (no seams), each tile one GPU submission, made smaller while one takes
  longer than 6 ms, so the game's frames do not wait on a long one. ncnn's fp16 output against the CPU's int8 models:
  PSNR 40–48 dB on a brick pattern (relief_check); against the float models 58 dB. Neither XNNPACK (slower than MLAS
  on a Galaxy S23) nor NNAPI (deprecated) is used.
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
