# Embedded torrent downloads

This engine-owned library builds `libstorm_torrent.so` for Android arm64/API 24.
It embeds libtorrent 2.0.15 (`1eb18faeae156d8dbbab42935c082f8b81f50989`), its
try_signal submodule (`105cce59972f925a33aa6b1c3109e4cd3caf583d`), Boost 1.90.0
headers, and OpenSSL 3.5.9 LTS. Exact archive SHA256 checksums are in
`tools/android/build_torrent.py`; license texts are in `third_party/licenses`.
No torrent CLI or Windows executable is run.

Build and test on a Linux host:

```sh
python3 tools/android/build_torrent.py --work /tmp/storm-torrent --jobs 4
python3 tools/android/build_torrent.py --work /tmp/storm-torrent --jobs 4 \
  --ndk /absolute/path/to/android-ndk-r30
```

The first command builds and runs a synthetic loopback-only BitTorrent fixture
(transfer, resume repair, invalid magnets, stall time, free-space check), metadata
discovery fixtures, JSON builders under allocation failure, production JNI
allocation-fault tests, and a real JVM `-Xcheck:jni` Unicode test.
The second builds JNI; it does not build the engine, install an APK or access a
device. Build dependencies: Python 3.12+, CMake 3.20+, Ninja, C++17, Perl, make
and JDK 17 (required for host JNI regression tests).
Package only `android/libstorm_torrent.so` and the NDK's matching `libc++_shared.so`.
All embedded dependencies are statically linked. Retain their license notices.

## Java contract

`org.stormengine.port.NativeTorrents` loads `storm_torrent` and declares these
static native methods (operations can throw `IOException`):

```java
String inspect(String localTorrentPath);
long open(String localTorrentPathOrMagnet, String stagingDirectory);
String poll(long session);
void start(long session, int[] selectedFileIndices);
void pause(long session);
void close(long session);
```

Each persistent library set has its own app-private directory. Copy a SAF torrent
document to a bounded private file before `inspect/open`. `open` downloads only
magnet metadata; a magnet without a well-formed info-hash fails with
`Invalid magnet link: …`. It never enables payload automatically, including on
resume. `start` downloads any explicitly selected selectable (non-padding) files,
at most 100000; it checks free space first (`Not enough free storage for selected
payload`). `poll` returns JSON:

```json
{"state":"downloading","downloadedBytes":0,"wantedBytes":123,"bytesPerSecond":0,
 "peers":0,"seeds":0,"stalledSeconds":95,"networkError":"Tracker udp://…: timed out",
 "checkpointPending":false,"error":"","errorKind":"",
 "payloadDirectory":"/private/library/set-id/payload","metadata":{…}}
```

States are `metadata`, `awaiting-selection`, `downloading`, `paused`, `complete`,
and `error`. Nothing fails silently:

* `error` carries the first fatal cause and `errorKind`: `storage-full` (no space
  for payload or checkpoint), `storage` (other disk errors), `transfer` (other
  torrent errors) or `metadata` (invalid metadata). The transfer pauses; `start`
  again clears the error and retries.
* `stalledSeconds` counts seconds without new verified bytes (or metadata) while
  in `metadata` or `downloading`; the application decides when that is a stall.
  `networkError` is the latest non-fatal tracker or listen-socket failure.
* Allocation failure makes a call throw; it never yields truncated JSON or a
  changed metadata snapshot (`tests/torrent/test_json_faults.cpp`).

## Metadata, version 2

Metadata is built once per session and cached. Every candidate is found from
file names alone (`evidence:"filenames-only"`, `verifiedGame:false`) and they are
never combined:

```json
{"metadataVersion":2,"name":"bundle","totalBytes":30,"pieceBytes":16384,
 "candidate":true,"verifiedGame":false,"evidence":"filenames-only",
 "kind":"installer-candidate","candidates":[
  {"id":"installer:0","kind":"installer-candidate","root":"bundle","rootKey":"bundle",
   "indices":[0],"companionIndices":[1],"companionHintsTruncated":false,"relativePaths":[]}],
 "files":[{"index":0,"path":"bundle/setup.exe","bytes":10,"recommended":true,"selectable":true},
          {"index":1,"path":"bundle/setup-1.bin","bytes":20,"recommended":true,"selectable":true}]}
```

* `loose-game-candidate`: `PROGRAM` and `RESOURCE` directly below the same
  parent, at any wrapper depth (plus that parent's `engine.ini`); separate roots
  are never joined. `relativePaths` canonicalizes `PROGRAM`, `RESOURCE` and
  `engine.ini` below the root. Executables and archives inside a game root
  belong to it and are not candidates of their own.
* `installer-candidate`: every other `.exe` (inert data, never run). Same-directory
  BIN, DAT, CAB, PAK, ARC, PCK and numeric-extension files are `companionIndices`
  hints, at most 64 (`companionHintsTruncated`).
* `archive-candidate`: ZIP, 7z, RAR, ISO, TAR and compressed TAR, CAB, ARC, and the
  volume families `.partNN.rar`, `.NNN`, `.rNN`/`.zNN` and `.partNN` of one
  directory and base name, ordered by part.

`kind` is the single candidate kind, `mixed-candidates` or `unknown`.
`recommended` marks the files of a single candidate and its hints only. Paths
keep their case; files or directories differing only in ASCII case, a file
shadowing a directory, and colliding `path`/`path.utf-8`/v2 file-tree spellings
are rejected. A finished selection is imported by the application from the
payload directory, which discovers the actual content; names prove nothing.

## Lifecycle and storage

Own the session in an Android foreground service with an ongoing notification;
Activity recreation must not destroy it. Call `poll` regularly (about once per
second) to consume alerts, report errors and pause completed downloads. Call
`pause`, then keep polling until `checkpointPending` is false, then `close` on a
worker thread. Closing can block while network/disk threads drain. Cancel means
pause and close; retain the directory for resume unless the user explicitly
deletes the set. Persist the source, selected indices and per-set directory in
the application, but restore the download only after an explicit user action.
Polling automatically requests an asynchronous checkpoint every 30 seconds.
A synchronous `torrent.id` marker identifies the set before payload writes, so
recovery also works after a process dies before its first checkpoint. Existing
files are hashed again on resume; piece bits from the checkpoint are not trusted.

Paths, duplicate names, traversal, symlinks, file count and total size are checked
before selection. Staging directories must not be shared with other writers.
Free space includes a boundary-piece reserve; archive expansion needs its own
space check. BitTorrent hashes verify pieces, not the legitimacy or suitability
of game content. Selected v1 files can require boundary pieces containing bytes
of adjacent unselected files; those bytes stay in libtorrent's private part file.

HTTPS tracker/web-seed certificates remain verified. The builder applies a narrow
Android trust-store patch to libtorrent, loading system PEM roots individually
from Conscrypt APEX, with the legacy system trust directory as fallback. No verification bypass is
used. DHT/PEX, UDP, HTTP and HTTPS trackers are supported; private torrents retain
libtorrent's protocol restrictions. Tracker reachability and Android background
execution still depend on network and OS lifecycle conditions.

References: [libtorrent API](https://www.libtorrent.org/single-page-ref.html),
[libtorrent releases](https://github.com/arvidn/libtorrent/releases),
[OpenSSL sources](https://openssl-library.org/source/),
[Boost source checksum](https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2.json).
