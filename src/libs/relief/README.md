# Relief maps from DeepBump

Normal maps for the colour textures that get relief (fx `ClassifyTextureName`: stone, wood, building atlases,
plaster, ground) are made from the player's game files with DeepBump (`fx/deepbump.hpp`) through onnxruntime, and
kept in the relief directory (`fx/relief_store.hpp`). Nothing derived is shipped. Grey "bump" textures are height
maps; the game derives their maps itself. The owner's choice: DeepBump's normals as the model gives them, at its own
strength (no rescale).

## Where the maps are made

In the game process, lazily (renderer_bgfx `src/effects/relief_generator.hpp`): a texture loaded without its map is
requested; one worker thread (nice 10, onnxruntime on all cores but two, paused by the Android thermal status from
MODERATE up) makes the most recently drawn first, then the rest of the location, then, while relief is on and 30 s
after the last request (not while a location loads), the other relief textures of the game by name (`List()`). A map
that becomes ready is uploaded and its relief fades in over 0.3 s. A complete pass prunes stale maps (other games,
older methods) and leaves `complete.txt` (`MarkComplete`): the next starts skip the pass while it matches. A map's key holds its method (`fx::ReliefMethod`: the method version, the
model's digest `fx::kDeepBumpModelDigest`, the class's options with the derivation's version, the map size), so a
change never reuses a map made before it; a pass that starts without a matching mark first prunes the maps of other
methods. The maps are a cache: on Android the relief directory is in the app's cache, so «Очистить кэш» (or the
system evicting it) removes them and the game makes them again lazily; a complete pass whose maps went missing
since is resumed (logged with their count). At start the stores of earlier builds are removed (logged): the files
directory's `relief`, kept across «Очистить кэш» (the app removes the old locations in its caches, GameCache); the
store in use is never removed as an old one. Each texture logs the key and method of its map when it is
looked up (`effects: relief <name> (<class>): map <key> of method <method>`). The model
is loaded at the first job and released after 10 s with no work left. The "effects relief:" log line counts the
textures waiting and the rest still to make; each map logs its stages (`relief <name>: WxH, N ms (read, decode,
model of which inference, mips, encode, write)`).

`storm_relief_cli <RESOURCE/Textures> <relief dir> <model.onnx> [map size] [threads]` makes a whole game at once:
host renders with every map in place (engine.ini `relief_dir = <relief dir>`). A host engine makes maps itself only
with engine.ini `relief_model = <path of model/deepbump256-int8.onnx>`; without it, it logs that no maps are made.

## Model and runtime

- Model: `model/deepbump256-int8.onnx` (GPL-3.0), derived from HugoTini/DeepBump at fad19ba8 by `model/make_model.py`
  (`model/README.md`; provenance in `third_party/licenses/DeepBump-PROVENANCE.txt`). One inference per texture on
  the whole image, wrap-padded by `fx::kDeepBumpPad` to a multiple of 32.
- Runtime: onnxruntime 1.31.1 (MIT), Microsoft's prebuilt Android AAR (`libonnxruntime.so` of the ABI, C API) and Linux
  x64 release on the host; notices in `third_party/licenses/onnxruntime-*`. The CPU execution provider (MLAS kernels),
  waiting threads sleep (no spinning). The XNNPACK provider aborts inside onnxruntime 1.31.1 while the session is
  created (x86_64 emulator), so it is not used; NNAPI is deprecated.
- Maps: DeepBump runs on the texture downscaled to 512 px (`fx::kReliefMapSize`, the owner's choice); the GPU's
  bilinear filter upsamples it over the texture. Высокая uses it whole, Средняя from its second mip. EAC RG11 with
  mips.

## Android

The engine build puts `libonnxruntime.so` (libengine.so links it) and `assets/deepbump256-int8.onnx` into
`<build>/game`. The app packages the library next to libengine.so, the model as the asset `deepbump256-int8.onnx`
(the engine reads it through SDL_RWFromFile), and the four licence files above on its licences screen. The relief
directory: the engine's cache root (`fs::GetCachePath()`: the app's `(getExternalCacheDir() ?: getCacheDir())/engine`)
+ `/relief` (engine `Renderer`).
