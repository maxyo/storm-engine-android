# Warm builds: ccache for every native build

Rule: **never build cold except in exceptional cases.** Every native build of the engine, the UI
runtime, the archive and torrent readers, host tools and CMake-based tests, in every worktree
and in the application's engine submodule, compiles through `tools/ccache/launcher` into one
shared cache, so a new worktree or build directory of already compiled sources costs cache hits
instead of a cold compile.

## How it works

- `cmake/StormBuildCache.cmake` is included right after `project()` by every Storm CMake
  project. It sets `CMAKE_C/CXX_COMPILER_LAUNCHER` to `tools/ccache/launcher` (replacing a
  plain `ccache` launcher, which would use another project's cache) and **fails configuration**
  when ccache is missing or a different launcher is set.
- Build scripts pass `-DCMAKE_C/CXX_COMPILER_LAUNCHER=<repo>/tools/ccache/launcher` to the
  third-party CMake projects they configure (SDL, fmt, zlib, xz, bzip2, libarchive) and
  `CC="<repo>/tools/ccache/launcher <compiler>"` to make builds (OpenSSL in
  `tools/android/build_torrent.py`, FFmpeg in `tools/ffmpeg/build.py`).
- `tools/ccache/launcher` sets these defaults unless already set in the environment, then runs
  ccache. Setting `CCACHE_DIR` also makes ccache ignore `~/.config/ccache/ccache.conf`, which
  belongs to another project: do not edit it, its cache, or run `ccache -C/-z` on it.

  | Setting | Value | Why |
  | --- | --- | --- |
  | `CCACHE_DIR` | `~/.cache/ccache-storm` | our own cache and config, isolated from the other project |
  | `CCACHE_BASEDIR` | the workspace holding all worktrees (`/home/.../corsars`) | absolute paths under it become relative to the build directory, so identical sources in different worktrees hit the same entries |
  | `CCACHE_NOHASHDIR` | `1` | the build directory path (in `-g` output) does not split the cache per worktree |
  | `CCACHE_COMPILERCHECK` | `content` | the same NDK/compiler at different paths or with fresh mtimes counts as the same compiler |
  | `CCACHE_SLOPPINESS` | `include_file_ctime,include_file_mtime` | fresh checkouts and agents editing during a build do not disable direct-mode hits |
  | | `time_macros` | `src/libs/core/src/compiler.cpp` uses `__DATE__ __TIME__` in the script-cache key; without it that file never hits. Its key also contains the git revision, so a cached object only repeats a build of the same commit, as an incremental build already does |
  | | `locale` | agents run with different `LANG`/`LC_*`; that only changes diagnostics language |
  | | (no `pch_defines`) | no project uses precompiled headers (RmlUi's are switched off) |
  | `CCACHE_MAXSIZE` | `30G` | ~105 GB were free; the other project reserves 25 GB plus 8 GB headroom. 30 GB compressed holds several build types of engine, UI, host and tools for many commits |
  | `CCACHE_COMPRESS` | `1` | zstd, roughly 3-5x smaller objects with debug info |
  | `CCACHE_STATSLOG` | `<build dir>/ccache-stats.log` | per-build statistics for `warmth` (Ninja runs commands in the build directory) |

  With `CCACHE_BASEDIR` the compiler gets workspace paths relative to the build directory:
  `__FILE__` and the debug-info file names are relative (`../../src/...`) and identical in all
  worktrees, so they do not break hits. `DW_AT_comp_dir` of a hit names the worktree that
  first compiled it; symbolication (addr2line, symbol upload) is unaffected.
- Never put absolute paths into compile definitions (`-DX="/abs/path"`): ccache does not
  rewrite them, so every worktree misses. Generate a header into a build include directory and
  include it by name, as `src/libs/diagnostics/include/watermark.hpp` does.
- Cross-worktree hits need **the same build directory depth** in the worktree (relative paths
  must match): `build/<name>` everywhere, or the script defaults (`build/android-ci/<name>`).
- Pinned FetchContent sources (bgfx, etcpak, glm, RmlUi, FreeType, lunasvg, json) are fetched once
  per machine into `~/.cache/storm-deps/fetchcontent/<name>-<digest of the pin>`
  (`storm_fetch_declare` in `cmake/StormBuildCache.cmake`, `STORM_DEPS_CACHE` overrides the
  root): pristine copies, shared read-only by all build directories. Unpatched ones are compiled
  from there (absolute paths, the same for every build at any build directory depth).
- Patched ones (bgfx, RmlUi) are compiled from the build directory's own view
  `<build dir>/_deps/<name>-patched-<digest of the pin>` (`storm_patched_view`): real directories
  whose files are symbolic links into the pristine copy, except the files a patch changes, which
  are real, patched files. The view's path does not depend on the patches, so editing a patch
  keeps every other compile command and ccache key, and the build stays incremental: linked files
  keep the pristine timestamps, a patched file is rewritten (with a fresh timestamp) only when its
  content changes, so Ninja rebuilds exactly its dependents. The patches are configure dependencies:
  adding, removing or editing one reconfigures the build directory on its next build.
  `FETCHCONTENT_SOURCE_DIR_<NAME>` values inside the shared cache or a view are this mechanism's own
  choice and are recomputed; an explicit override elsewhere must carry every patch or configuration
  fails.
- The host `shaderc` is built once per machine into `~/.cache/storm-deps/shaderc-<key>` by the
  first configure that needs it, under a lock (`src/libs/renderer_bgfx/cmake/storm_shaderc.cmake`);
  `-DSTORM_SHADERC=<binary>` overrides. The key covers the pin and only the bgfx patches that change
  a file shaderc compiles or includes (recorded from Ninja's dependency log of every shaderc build in
  `~/.cache/storm-deps/shaderc-inputs-<pin key>.txt`), or that create a file: a renderer-only patch
  reuses the existing shaderc. A needed shaderc build compiles with its own build directory as
  `CCACHE_BASEDIR`, so only the files its patches change miss the cache; its build directory is
  removed afterwards (only the binary is used).

## Is this build warm?

Build scripts wrap the build step:

```sh
tools/ccache/warmth begin <build dir>   # after configure: fails unless the launcher is set
cmake --build <build dir> ...
tools/ccache/warmth end <build dir>     # prints: N compilations, H hits (x%), M misses
```

`end` raises the **ХОЛОДНАЯ СБОРКА / COLD BUILD** alarm and exits 1 when a *fresh* build
directory (nothing compiled in it before) misses more than 100 compilations at a hit rate below
60%. Misses of an incremental build are explained by the edits Ninja found and only reported.
Genuinely new sources (the first fill of the cache, a widely included header changed on a branch
nobody built yet) pass with `STORM_ALLOW_COLD_BUILD="<reason>"`, printed with the alarm.

Before starting a big build in a worktree:

- `ninja -C <build dir> -n <target> | tail -1` shows how many steps would run. Hundreds of steps
  in a fresh directory are fine when they are cache hits; check with `warmth` above, or watch
  `<build dir>/ccache-stats.log` grow with `direct_cache_hit` lines.
- No `CMakeCache.txt` means a configure from scratch: run it from the build scripts or with the
  same `cmake -S/-B` layout as the other worktrees (same depth).
- `tools/ccache/check-build-dirs` lists every build directory of the workspace without the
  launcher (exit 1 if any); reconfigure those with `cmake -S <source> -B <build dir>`.
- `tools/ccache/launcher -s` shows the totals of the shared cache with the project settings
  (any ccache option works through the launcher; never `-C`/`-z` on another cache).

## The exception: building without the cache

Only for an exceptional case (for example measuring a cold compile), configure with
`STORM_ALLOW_NO_CCACHE="<reason>"` (environment or `-D`; `1`/`ON` are rejected: the value is
the reason). Configuration warns loudly, and every compile prints
`STORM ALARM: UNCACHED COMPILE` with the reason through `tools/ccache/uncached`.
A `-D` value stays in the build directory's cache: end the exception with
`cmake -U STORM_ALLOW_NO_CCACHE <build dir>`.

## Scripts that run the compiler directly

Checks in `tests/*/run.py` and `tools/android/test_*.py` that call `$CXX` themselves get
`CXX=<repo>/tools/ccache/cxx` from `tools/ci/run_host_tests.py` and
`tools/android/ci_dependencies.sh`; it runs `$STORM_CCACHE_CXX` (the real compiler, default
`c++`) through the launcher. Running such a check by hand:
`CXX=tools/ccache/cxx STORM_CCACHE_CXX=clang++ python3 tests/<name>/run.py`. Their `-c` compiles
are cached; single-step compile-and-link commands (`$CXX check.cpp -o check`) are not cacheable
by ccache, and are small.
