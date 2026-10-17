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

The first command builds and runs a synthetic loopback-only BitTorrent fixture,
production JNI allocation-fault tests, and a real JVM `-Xcheck:jni` Unicode test.
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
magnet metadata. It never enables payload automatically, including on resume.
`start` downloads the exact supported file selection. `poll` returns JSON:

```json
{"state":"awaiting-selection","downloadedBytes":0,"wantedBytes":0,
 "bytesPerSecond":0,"peers":0,"checkpointPending":false,"error":"",
 "payloadDirectory":"/private/library/set-id/payload",
 "metadata":{"name":"Example","totalBytes":123,"pieceBytes":16384,
 "candidate":true,"verifiedGame":false,"kind":"installer-candidate",
 "files":[{"index":0,"path":"Example/setup.exe","bytes":123,"recommended":true}]}}
```

States are `metadata`, `awaiting-selection`, `downloading`, `paused`, `complete`,
and `error`. Metadata kinds are `loose-game-candidate`, `installer-candidate`,
`archive-candidate`, `unknown`. Metadata file names **never establish game identity**.

For Inno installers first select only `setup.exe`, download it, and use the archive
library to inspect its embedded installation manifest before selecting sibling
`setup-*.bin` files. The exe remains inert data. Do not automatically extract or
publish arbitrary content based on the torrent title. After all necessary pieces
complete, extract into a different per-set directory, then run the application's
strong final game-content validator. Only that completed set can be made playable.
Never merge one set into another or into a running game's directory.

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
