# Relief preparation («Подготовка рельефа»)

Normal maps for the colour textures that get relief (fx `ClassifyTextureName`: stone, wood, building atlases,
plaster, ground) are made once from the player's game files with DeepBump (`fx/deepbump.hpp`) through onnxruntime,
and stored in the relief directory (`fx/relief_store.hpp`). The game only reads them: a texture without its map has no
relief, and the game logs how many are missing (`effects relief:` line). Grey "bump" textures are height maps; the
game derives their maps itself. The owner's choice: DeepBump's normals as they are (overlap MEDIUM), each scaled to
the RMS tilt of our own luminance derivation of the same texture, at full strength.

- Model: `deepbump256.onnx` from HugoTini/DeepBump at commit fad19ba8 (GPL-3.0); provenance and checksums in
  `third_party/licenses/DeepBump-PROVENANCE.txt`. The build fetches it pinned and checks its sha256.
- Runtime: onnxruntime 1.31.1 (MIT), Microsoft's prebuilt Android AAR (`libonnxruntime.so` of the ABI, C API) and Linux
  x64 release on the host; notices in `third_party/licenses/onnxruntime-*`. The CPU execution provider (MLAS kernels)
  runs the model on every core, logged as `CPU, N threads`. The XNNPACK provider aborts inside onnxruntime 1.31.1 while
  the session is created (x86_64 emulator, three option sets tried), so it is not used; NNAPI is deprecated.
- Maps: DeepBump runs on the texture downscaled to 512 px (`fx::kReliefMapSize`, the owner's choice); the map stays
  512 and the GPU's bilinear filter upsamples it over the texture (the shader renormalises the normal). Высокая uses
  it whole, Средняя from its second mip. EAC RG11 with mips; all of TEHO ≈ 165 MB.

## Android (launcher process)

The build puts into `<build>/game`: `libstorm_relief.so`, `libonnxruntime.so`, and `assets/deepbump256.onnx`. The app
packages the two libraries next to `libengine.so`, the model as the asset `deepbump256.onnx`, and the four licence files
above on its licences screen.

```java
final class NativeRelief {
    static { System.loadLibrary("onnxruntime"); System.loadLibrary("storm_relief"); }
    interface Progress { boolean onProgress(int done, int total, String texture); } // true cancels
    // JSON: status (0 done, 1 cancelled, 2 error), total, prepared, existing, failed, skipped, tiles, seconds,
    // removed, infer_seconds, max_texture_ms, peak_rss_kb, provider[, error]
    static native String prepare(AssetManager assets, String modelAsset, String texturesDir, String reliefDir,
                                 int threads, Progress progress);
    static native int[] check(String texturesDir, String reliefDir); // [relief textures, missing maps]
}
```

- `texturesDir`: the game's `RESOURCE/Textures` (as found under the game root, any case).
- `reliefDir`: `(getExternalFilesDir(null) ?: getFilesDir()) + "/relief"` — the directory the engine reads
  (`Renderer`, engine.ini `relief_dir` overrides on the host).
- Map size: `fx::kReliefMapSize` (one constant, engine side; the game reads that size). `threads`: 0 = every core.
- Synchronous: run on a worker thread; the callback runs on it after each texture (and once at the start with
  done = 0). Resumable: maps already there are kept, so a cancelled or interrupted run continues where it stopped.
- `check` is cheap (reads headers and one small level per texture): use it after an import or an app update to
  offer the step when `missing > 0`.

## Host

`storm_relief_cli <RESOURCE/Textures> <relief dir> <deepbump256.onnx> [map size] [threads]` prepares a game copy;
`--check` counts. Host renders with prepared maps: engine.ini `relief_dir = <relief dir>`.
