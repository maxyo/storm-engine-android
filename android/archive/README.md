# Safe game archive import library

This engine-owned library reads installer/archive **data** into an empty private
staging directory. It never launches an installer, loads an extracted library,
evaluates installer scripts, or applies registry/run entries. The CLI/process/GOG
execution helpers from innoextract are not compiled. There is no external
archive-helper fallback.

The Android app owns document selection, copying SAF documents into private
input storage, retaining separate source sets, and publishing a validated staging
directory. A failed/cancelled staging directory must be deleted in full. Never
extract into a live game, saved-game directory, or an existing game set.

## Build and package

From the engine checkout, using an existing NDK and a dedicated cache:

```sh
python3 android/archive/build.py \
  --build-dir /absolute/local-testing/archive-loader/ndk \
  --ndk /absolute/android-ndk-r30 --jobs 4
```

The script fetches only SHA-256-pinned upstream source releases, builds static
readers, then links `output/libstorm_archive.so` for Android ARM64/API 24 with
16 KiB ELF alignment and shared libc++. All dependencies are owned by this
engine. Package that `.so`, the engine's matching `libc++_shared.so`, and the six
license notices in `android/archive/licenses/`. No source/game payload belongs
in the application repository or APK. `--sources` reuses a dedicated source
cache; it must not point to a cache being patched by another build.

Omit `--ndk` for a host library and `reader/storm_archive_cli`. Standalone CMake
expects the pinned sources and prepared prefix from `build.py`.

## Android JNI contract

Class: `org.stormengine.port.NativeArchives`.
Load: `System.loadLibrary("storm_archive")`.

```java
static native String[] inspect(String localInputPath);
static native String[] manifest(String localInputPath);
static native String[] discover(String source, String emptyScratch, String passwordOrNull,
                                long[] limits, Progress callbackOrNull);
static native String[] extractRoot(String source, String scratch, String passwordOrNull,
                                   String rootId, String emptyPrivateStaging,
                                   long[] limits, Progress callbackOrNull);
interface Progress {
    boolean onProgress(int stage, String path, long bytes, long totalBytes, long filesCreated);
}
```

All calls are synchronous and belong on a worker thread. The callback runs on
that same thread and returns **true to cancel**. It may fire while reading or
skipping compressed data, including with an empty path. `filesCreated` counts
opened output files, not completed/checksummed files. Successful completion is
only status 0 from `extractRoot`.

Results are `[decimalStatus, format, error]`; `manifest`, `discover` and
`extractRoot` add a fourth JSON string. Inspect only identifies the container;
**manifest** previews one installer without its volumes (the torrent library
uses it to fetch `setup.exe` first). Imports use **discover** and
**extractRoot** (below). C consumers use `storm_archive.h`; the single-container
C call `storm_archive_extract` remains for C users and the regression tests.

| Status | Meaning |
|---|---|
| 0 | Success |
| 1 | Input/output failure |
| 2 | Unsupported archive, installer version, or feature |
| 3 | Unsupported executable installer; no code was run |
| 4 | Encrypted input; passwords are not supported |
| 5 | Invalid/corrupt input, unsafe path/link, duplicate, or conflicting roots |
| 6 | Entry, byte, memory, or time limit |
| 7 | Cancelled |
| 8 | Missing Inno sibling volume |
| 9 | Required game structure missing |
| 10 | Not enough free storage (JSON says how much is needed) |

The optional limits array contains `[totalBytes, fileBytes, entries, seconds]`.
Defaults are 32 GiB, 4 GiB, 200000, and 3600. JNI converts ordinary UTF-8 to/from
UTF-16, including supplementary characters; it does not pass UTF-8 filenames to
JNI Modified UTF-8 functions.

Manifest JSON contains:

```json
{
  "format": "inno",
  "installer_version": "5.6.0 (unicode)",
  "metadata_only": true,
  "compatible_structure": true,
  "has_resource": true,
  "has_program": true,
  "has_engine_ini": true,
  "unpacked_bytes": 123456,
  "file_count": 3,
  "required_volumes": ["setup-1a.bin", "setup-1b.bin"],
  "files": [{"path": "RESOURCE/example.txt", "size": 123}]
}
```

A structurally incompatible manifest returns status 9 **with JSON**, allowing an
explanatory preview. Structural compatibility is not proof of authenticity or
complete game content. Manifest metadata does not verify payload checksums; only
successful extraction verifies the selected file bytes. A large manifest should
be summarized for the UI rather than rendered as thousands of rows immediately.

For a standard multipart Inno installer, the complete `setup.exe` is enough for
this preview. No `.bin` content is read by `manifest`. A torrent client can fetch
that executable first, validate the manifest, and then fetch the exact
`required_volumes`. For extraction all those volumes must be fully downloaded
alongside `setup.exe`, with the exact names, in app-private input storage. A
single SAF file descriptor cannot represent this multipart set; copy the user-
selected source documents first. Keep each source set in its own directory.

## Formats and boundaries

* Inno Setup: innoextract 1.9 parser, known Unicode versions supported by that
  release (through 6.0.5); unknown versions and ANSI installers fail closed.
  Standard embedded data and standard sibling `.bin` volumes are supported.
  Only literal `{app}` paths are considered; constants/code are not evaluated.
  GOG Galaxy reassembly, external file entries, conditional component selection,
  and multipart logical files are not supported.
* ZIP/ZIP64 (seekable, complete central directory required), 7z, tar, tar.gz,
  tar.bz2, tar.xz, Microsoft CAB, RAR/RAR5, ISO9660: libarchive 3.8.10 readers.
  Codec support is limited to the linked zlib, bzip2, liblzma and libarchive's
  built-in codecs. Not every RAR/CAB/7z variant is supported.
* Generic EXE/SFX, NSIS, MSI, InstallShield CAB, encrypted archives and unknown
  installer packers are not advertised as supported. In particular, Microsoft
  CAB and InstallShield CAB are different formats. No installer is run as a
  fallback.
* The legacy single-container calls (`manifest`, `storm_archive_extract`) do
  not recurse and require one game root. Source discovery (next section) reads
  nested containers, multi-volume sets and several game roots.

The output whitelist is a single `RESOURCE/`, `PROGRAM/`, `engine.ini`, and
`options` root; packaging wrappers such as `app/` are removed. Other files are
not written. Executable/library/shell extensions are excluded even under the
selected roots. In-game `PROGRAM` scripts are data needed by the game engine;
the importer does not evaluate them.

Paths must be relative, contain no traversal, control characters, drive/ADS
colon, empty components, or Windows trailing dots/spaces; limits are 1024 bytes,
32 components and 255 bytes per component. Symlinks, hardlinks and special files
are rejected. Output uses `openat`/`O_NOFOLLOW`/`O_EXCL` under a held directory
file descriptor; existing outputs and ASCII case collisions are rejected.
All input volumes must remain private and unmodified during import.

The C archive/codec allocators are capped at 768 MiB per library instance. Inno
parsed metadata is independently capped at 128 MiB, individual binary strings at
16 MiB, and LZMA dictionaries at 512 MiB. Some real installers use a 512 MiB
LZMA dictionary: expect substantial native-memory pressure on low-memory
phones. Exact dictionary memory is encoded in the data stream and generally
cannot be learned from `setup.exe` alone. Allocation failure returns status 6;
it does not trigger an installer fallback. Every libarchive initialization result
is checked, including callback registration. Each reader owns an intrusive C
allocation arena that needs no separate allocation. On OOM or partial
initialization, that arena releases all C/codec memory without re-entering a
poisoned reader. Input/output descriptors remain owned by C++ RAII objects.

The pinned libarchive fatal-OOM hook is adapted to unwind to the C++ API boundary
instead of calling `exit`/`abort`; its C sources are built with `-fexceptions`.
The normal C allocators still return NULL. Callback error reporting passes a
null message to `archive_set_error`, avoiding allocation inside `noexcept`
recovery. Normal cleanup is guarded against exceptions. No `longjmp` crosses
C++ objects. libarchive receives only custom read/seek callbacks, and iconv and
external helper programs are disabled, so the arena owns all reader/codec C
allocations and no separate OS resources. Time/cancellation is checked at
archive reads and extraction blocks; metadata parsing itself is bounded by its
size limits.

## Source discovery and explicit root selection

`discover(source, scratch, password, limits, progress)` finds **every complete
game root** (a folder holding `engine.ini`, `PROGRAM/` and `RESOURCE/`, ASCII
case-insensitive) in a private source and returns
`[status, "", error, json]`. `source` is a directory (copied picker documents,
searched recursively) or one file in a directory (only that file and its own
volumes are read; the torrent library passes the chosen archive). Inside it:

* loose game folders (`kind:"folder"`);
* ZIP/7z/RAR/tar(.gz/.bz2/.xz)/CAB/ISO archives (`kind:"archive"`), recognised by
  signature, also when renamed;
* Inno Setup installers (`kind:"inno"`) with their standard sibling volumes;
  metadata only, missing volumes make the root `ready:false` and are named;
* multi-volume archives: raw byte-split sets (`game.7z.001`, `.002`, ...), RAR
  `game.part1.rar`... and `game.rar` + `game.r00`...; a gap is named, a set that
  ends early names the next part. Split ZIP (`.z01`) is reported as unsupported;
* containers inside containers, up to `maxDepth` (1..3, default 3) levels below
  the source. Their members are written to `scratch/<n>/<member path>` and read
  like a source folder, so an installer with its `.bin` files inside a ZIP, an
  installer inside an archive inside an archive, or a split set inside an
  archive all work. Members inside a found game root (its executables, DLLs)
  and game data are never treated as containers. Data inside an Inno payload is
  not searched for further containers.

Each root is a separate candidate; roots are never merged. JSON version 2:

```json
{"version":2,
 "candidates":[{"id":"…","kind":"archive","format":"zip","location":["download.zip","repack.7z"],
   "root":"Game/","title":"Корсары: Каждому Свое","version":"ver. 1.7.3 (26.11.21) x64",
   "languages":["english","russian"],"default_language":"russian","file_count":46418,
   "unpacked_bytes":12734518486,"patch_markers":["Patch_1.3_readme.txt"],
   "ready":true,"missing_volumes":[]}],
 "issues":[{"location":["menu.exe"],"status":3,"error":"…","missing_volumes":[]}],
 "partial_roots":[{"location":["half.zip"],"root":"Game/","has_engine_ini":false,
   "has_program":true,"has_resource":true}],
 "scratch_bytes":3624000000}
```

Metadata is read where it is cheap: title/version from `PROGRAM/_LSC_on_off.h`
(`VERSION_NUMBER1/2`, Windows-1251 converted), default language from
`RESOURCE/INI/texts/language.ini`, for folders and ZIP/ISO/CAB; installers give
their header name/version. Languages are the `RESOURCE/INI/texts/<language>/`
folders; `patch_markers` are root-level files named like patches/updates. An
unreadable child is an `issue` (status, error, missing volumes) and healthy
siblings stay available; `partial_roots` explain incomplete copies.
`file_count`/`unpacked_bytes` are exactly what `extractRoot` writes.

`extractRoot(source, scratch, password, id, staging, ...)` writes exactly the
root named by the opaque `id` (from the same source and scratch) into an empty
staging directory, under the canonical names `RESOURCE/`, `PROGRAM/`,
`engine.ini`, `options`. Files of other roots and wrappers are skipped. A source
that changed since discovery fails (status 5 or 8). The caller deletes scratch
afterwards and discards a failed staging directory.

Limits (`long[8]`, all positive):
`[totalBytes, fileBytes, entries, seconds, scratchBytes, maxDepth, maxRatio, reserveBytes]`;
defaults 64 GiB, 4 GiB, 400000, 3600, 32 GiB, 3, 100, 64 MiB. Bytes and entries
count everything one call decodes or lists. **Bomb guard:** declared sizes of
an archive and the bytes actually decoded from one container (archive or
installer with its volumes) may not exceed `maxRatio` times its input size plus
1 MiB. **Free space:** before each nested container is written and before
extraction starts, free space must cover the data plus `reserveBytes`; writes
re-check every 16 MiB and `ENOSPC` becomes status 10. Status 10 returns
`{"no_space":{"step":"nested"|"install","needed_bytes":N,"available_bytes":M}}`.

**Passwords:** an encrypted container returns status 4 with
`{"encrypted":{"location":[…],"password_supported":bool,"password_given":bool}}`.
Only ZIP traditional (ZipCrypto) encryption can be opened (no crypto library is
linked): pass the password to both calls. 7z/RAR/AES-ZIP/Inno encryption is
`password_supported:false`. A wrong ZIP password is reported as status 4 when
data is first decrypted (discovery reads metadata files; otherwise extraction).

Status 6 (limit, bomb, memory, time), 7 (cancelled) and 10 (space) end the
call; other container failures become issues.

## Verification

```sh
python3 android/archive/tests/test_archives.py \
  --library /absolute/build/reader/libstorm_archive.so -v
```

`--inno-installer /absolute/setup.exe` additionally validates the local reference
Inno 5.6 installer metadata without volumes, missing/corrupt volumes, and bounded
cancellation after starting the real decoder. This optional reference check
expects 46418 selected files and 12734518486 bytes; no game assets are included
in tests or Git. Synthetic tests exercise extraction, checksums, truncation,
encryption, path/link attacks, collisions, byte/count limits and cancellation.

Discovery and nested extraction (host library built with
`-DSTORM_ARCHIVE_TESTING=ON`, which adds simulated free space):

```sh
python3 android/archive/tests/test_discovery.py \
  --library /absolute/build/reader/libstorm_archive.so \
  [--libarchive-tests /absolute/sources/libarchive-3.8.10/libarchive/test] \
  [--inno-installer /absolute/setup.exe] -v
```

Fixtures are generated in the test: wrapped and case-distinct roots, two copies
in one archive, loose folder beside an archive, three-level nesting (zip › 7z ›
zip) and the depth limit, tar.gz in ZIP, raw split sets with missing middle/last
parts, single-file sources, declared and nested bombs, ZIP passwords (right,
wrong, at extraction), unsupported 7z header encryption, no space for nested
data and for installation, orphan Inno volumes, unsupported/split-ZIP/foreign
executables, partial roots, unsafe members, staged progress and cancellation,
scratch rules and changed sources. libarchive's own RAR multi-volume fixtures
check volume switching and a missing part; the optional real installer checks an
installer inside a ZIP without its volumes.

Allocation-failure regression (host-only controls are prohibited in Android
builds):

```sh
cmake -S android/archive -B /absolute/build/reader -DSTORM_ARCHIVE_TESTING=ON
cmake --build /absolute/build/reader --parallel 4
python3 android/archive/tests/test_oom.py \
  --library /absolute/build/reader/libstorm_archive.so \
  --report /absolute/local-testing/archive-oom.json
```

Every C allocation position for ZIP/gzip/7z inspect, manifest, extract,
cancellation and source discovery (also of a ZIP nested in a ZIP, written to
scratch) runs in an isolated subprocess with single and persistent failures,
three repeated failed calls, quota accounting, and a subsequent successful call.
Optional `--inno-installer /absolute/setup.exe` extends this to the reference Inno
metadata and decoder cancellation after 1 MiB; it never extracts the full game.
The fault controls affect this library's private C allocator, not the JVM or
platform loader. Platform process termination before entering the library (for
example a dynamic loader unable to initialize TLS) is outside this API boundary.

For host JNI encoding, staged callbacks, discovery and root extraction:

```sh
cmake -S android/archive -B /absolute/build/reader -DSTORM_ARCHIVE_HOST_JNI=ON
cmake --build /absolute/build/reader --parallel 4
javac -d /absolute/build/jni-test android/archive/tests/NativeArchives.java
java -Xcheck:jni -Djava.library.path=/absolute/build/reader \
  -cp /absolute/build/jni-test org.stormengine.port.NativeArchives
```

The pinned Inno source is patched only in the dependency build tree: bounded
metadata/dictionary allocations, a missing Boost include, and a fixed upstream
1.9 volume-signature loop bound. The CLI, external-process helpers and GOG
helpers are excluded completely. Build patches are reproducible and idempotent.

## Upstream and licenses

Versions, source URLs and SHA-256 digests are pinned in `build.py`; the original
notices are beside this README in `licenses/`.

| Dependency | License | Upstream |
|---|---|---|
| innoextract 1.9 | zlib | https://constexpr.org/innoextract/ |
| libarchive 3.8.10 | BSD notices in COPYING | https://www.libarchive.org/ |
| XZ liblzma 5.8.4 | 0BSD | https://tukaani.org/xz/ |
| bzip2 1.0.8 | bzip2 BSD-style license | https://sourceware.org/bzip2/ |
| Boost 1.90.0 filesystem/iostreams/headers | BSL-1.0 | https://www.boost.org/ |
| zlib 1.3.2 | zlib | https://zlib.net/ |

7-Zip itself is not linked (its broader format support does not add Inno parsing).
Unshield is not linked; adding its InstallShield backend requires separate
format validation, fixtures and pinned dependency review.
