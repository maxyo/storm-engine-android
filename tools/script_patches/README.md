# Script patches

Fixes to the player's own game scripts, applied by the engine at start-up
(`src/libs/util/include/platform/script_patches.hpp`, called from
`FILE_SERVICE::ScanResourcePaths` on Android). The application never ships game
files: it ships unified diffs against one exact version of a script, and only
for games whose scripts may be redistributed in modified form. The application
repository holds the patch sets and decides, per fix licence, what may be
packaged.

## Layer

Android: `<filesDir>/script-patches/current`, installed from the APK like the
other layers (sha256 per file in `manifest.json`). Without a `sets/` directory
the layer patches nothing.

```
sets/<set>/index.txt        one game version
sets/<set>/<patch files>    unified diffs, paths in index.txt
```

`index.txt`: UTF-8, LF, tab-separated records, first record is the format.

```
storm-script-patches  1
set       <set id = directory name>  <game>  <version>
identify  <game file>  <sha256>                       (one or more, all must match)
target    <game file>  <base sha256>  <result sha256>  <patch file>
fix       <fix id>  <target>[,<target>...]  <licence>  <source>  <title>
```

Patch files are `diff -u` output with `--- a/<target>` / `+++ b/<target>`
headers. Lines keep their bytes (CRLF scripts patch as CRLF), hunks apply at
their exact line numbers with exact context, and `\ No newline at end of file`
is honoured.

Allowed targets (`isPatchTargetPath`): `program/dialog.c`,
`program/dialogs/<language>/**` (each language file is its own target),
`program/quests/`, `program/sea_ai/`, `program/loc_ai/`, `program/weather/`,
`program/battle_interface/` (the town flag of locations without a town),
`program/locations/` and `program/interface/` (the tab-switch countdown of the hero screens),
`.c`/`.h` only; the list grows only when a real fix needs another directory.
`program/interface/` is also the UI overlay's directory: a file is never both replaced
and patched, so a target the overlay replaces is a damaged layer (`applyLayer` throws).
`program/controls` stays excluded.

## Behaviour

- **Set selection.** A set applies when all its `identify` files match. No
  matching set: nothing is patched and the log says so; other games and
  versions are not told about fixes that were never meant for them. Two
  matching sets: damaged layer.
- **Base mismatch** (a target missing or not the expected sha256): that fix is
  not applied, and neither is any fix sharing a script with it, so a fix never
  half-applies. The player's files are used unchanged. Each unapplied fix is
  logged as an error with the expected and actual sha256, and the player sees a
  one-time notice listing the unapplied fixes (shown again only when the set of
  mismatches changes; `<filesDir>/script-patches/notice-shown`). Base
  mismatches are never fatal.
- **Damaged layer** (bad index or patch file, a patch that does not apply to its
  own verified base, a result with the wrong sha256, two sets for one game):
  `LayerError`. The game does not start and says why; this is an application
  bug, caught by the host checks before release.
- **Derived files, not memory.** The engine opens scripts by path through the
  resource index, so applied targets are written to
  `<filesDir>/script-patches/derived/<sha256(base ":" patch)>/<target>` and the
  index points there. A file is written only when missing or damaged, keeping
  its mtime stable; directories of other bases or patch versions are removed.
- **Script cache.** The compiled script cache key covers the program tree and
  the patch layer (`programLayers`, `FILE_SERVICE::GetPathFingerprint`): a new
  patch, a new set or a changed player file recompiles.
- **Saves.** Saves store script variables by name and recompile segments from
  the current scripts, so code fixes load into existing games. A fix that
  renames or retypes a global, changes array sizes, or writes new attributes
  into saved objects changes save-visible state and must say so in its
  `save_compatibility` note (application repository).

## Host tool

`script_patch_tool.cpp` uses the same header:

```
c++ -std=c++20 -I src/libs/util/include tools/script_patches/script_patch_tool.cpp -o script_patch_tool
./script_patch_tool check <layer>
./script_patch_tool apply <layer> <game dir> <out dir>
```

Checks: `tests/script_patches/run.py`.
