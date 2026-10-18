Run `python3 tests/android_graphics_effects/run.py` for the production effect
preference snapshot test under ASan/UBSan. A fake JNI boundary records reads and
exercises independent on/off/default values and edits after initialization.
Each effect reads its preference once per process, so edits require restarting.

On Android, `character_shadows=0` returns before Shadow::Realize touches model,
render targets, blur or projection. Shadow resource ownership remains unchanged.
`sea_foam=0` disables the extra Sea2_Foam pass through the existing foam gate;
weather/script and engine.ini restrictions still apply when enabled. Defaults
preserve both effects. Desktop behavior is unchanged.

The test validates preference semantics, not GPU output. Runtime acceptance must
compare enabled/disabled character shadows and sea foam after a process restart,
including returning to a scene and restored render targets. Performance gains
require measurements on the target device; this test does not claim an FPS gain.
