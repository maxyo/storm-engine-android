# Open audio primitives and SoundService backend

The miniaudio backend is the only SoundService implementation (Android and the Linux host),
through `service_backend.cpp`. The
small RAII adapter uses real miniaudio mixing/decoding, either offline PCM or an
explicit real-device backend list (Android AAudio/OpenSL ES; no null fallback).

Sources are downloaded by `tests/audio/run.py` into the ignored build directory
and verified by SHA256 before compilation:

- miniaudio 0.11.23, official `mackron/miniaudio` tag; SHA256
  `7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6`.
  Choose the MIT-0 alternative in its embedded license.
- stb_vorbis, official `nothings/stb` commit
  `2c980bb59875b0d32144a71867fbdebb2f77cd20`; SHA256
  `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`.
  Choose the MIT alternative in its embedded license. Retain that notice when
  distributing binaries. The full engine APK retains both notices and the applied patch.

Both original source files retain complete upstream notices. A separate build
copy receives `miniaudio-0.11.23-error-cleanup.patch`: ASan exposed a
use-after-free on missing-file initialization. Notification cleanup now precedes
node destruction and the failed output pointer is cleared. The missing-file
regression is included in every run; sanitizer checks cover this error path. Vorbis floor-0
and chained streams remain unsupported; the synthetic corpus is not evidence
of compatibility with every game resource.

```
python3 tests/audio/run.py
python3 tests/audio/run.py --build-dir build/audio-asan --sanitize
python3 tests/audio/run.py --build-dir build/audio-android \
  --cxx /path/to/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++ \
  --compile-only
```

Host prerequisites: C++17 compiler, Python 3, ffmpeg with libmp3lame/libvorbis.
Fixtures are synthesized from a mono 440 Hz tone; no game assets are used.
The default run renders stereo 48 kHz PCM and asserts codec output, finite
samples, cursor, pause/resume, seek, pitch, loop wrap, fade endpoint, left/right
orientation, linear distance attenuation, concurrent music streams plus a 3D
effect, crossfade and 100 engine create/suspend/resume/destroy cycles.
Streaming seek is asynchronous and applied by the mixer; callers must not
assume sample-immediate completion. The test allows bounded worker progress.

`audio-probe <fixture-directory> --device` explicitly opens actual audio output,
plays WAV and streamed Vorbis, suspends for one second and resumes. Device
failure is an error. Android requires deploying the ARM64 binary, fixtures and
matching libc++_shared.so and setting LD_LIBRARY_PATH. It is a command-line
probe, not Activity lifecycle integration. Home/resume, lock/unlock, route
changes, underruns, latency and real game resources still need device tests.

A separate-process memory check compares two 3-second streams with two
180-second streams (MP3 + Vorbis), requiring peak RSS growth <=8 MiB. This is a
coarse regression bound, not a phone memory budget or a proof of constant memory
for arbitrary files. Linux peak RSS includes runtime/allocator overhead.
Offline suspend gates the manual PCM pump and preserves the cursor; only the
`--device` mode calls actual device stop/start. Do not report offline cycles as
Android lifecycle validation.

## Local validation evidence

Host GCC 14: PCM/controls/spatial/streaming and 100 offline cycles pass. The same
suite passes ASan + UBSan, including missing-file rejection after the cleanup
patch. Two-stream RSS comparison measured 17,664/17,664 KiB without sanitizers
and 20,040/19,900 KiB with sanitizers for 3s/180s inputs in these runs; values are
machine-specific. NDK r30/API24 links an ELF64 AArch64 PIE with
`--no-undefined`. Clang emits one upstream stb_vorbis pointer-comparison warning
at line 1404; it is not suppressed. Android execution remains unverified.


## Integrated service

`service_backend` owns the tested priority/virtualization and stamped-ID policy.
`tests/audio/service.cpp` also checks overflow, category gain behavior, zero pitch,
100 service lifecycle cycles and 44.1 kHz resources resampled to the engine's
48 kHz cursor clock. The full service bridge and retained legacy behaviors are
documented in the application repository’s `android/game/TESTING.md`. Source archives remain unchanged; only
the explicit miniaudio error-cleanup patch modifies the compilation copy.


## Sample cache, streaming and background decode

Decoded samples used to stay in memory for the whole session and were decoded
inside `SoundPlay` on the game thread. `Mixer` now applies `Limits`:

- **LRU cache** (`cacheBytes`, 48 MiB by default; `[sound] cache_mb` in
  `engine.ini`, 4..512). A decoded sample is pinned in miniaudio's resource
  manager while any voice references it or while it is among the most recently
  used; unused samples are released oldest first when the total exceeds the
  budget. Samples in use are never evicted, so the total can exceed the budget
  temporarily and drops back when their voices end. `Mixer::trim(0)` /
  `VSoundService::TrimCaches()` release everything unused (memory pressure).
- **Streaming long samples** (`streamBytes`, 4 MiB decoded, about 11 s of
  48 kHz stereo; `[sound] stream_threshold_mb`, 1..64). Longer effects and
  ambience are played through a miniaudio stream like music, costing about one
  second of PCM per playing voice instead of the whole file. They keep effect
  IDs and priorities; only the music type uses the two fixed music slots.
- **Background decode** (`asyncDecode`, on for a real device, off for the
  offline test mixer; `[sound] async_decode=0` disables it). `play()` only
  probes the file header (missing or unsupported files still fail
  synchronously) and queues the decode on one worker thread. Until it
  finishes the voice exists, reports as playing and holds its cursor at the
  start, so the sound starts a few milliseconds late instead of losing its
  attack. A failed decode ends the voice and is reported through
  `takeFailures()`.
- `duck(gain)` scales the master group independently of fades and category
  volumes (Android transient audio focus loss).

`tests/audio/service.cpp` covers eviction order and pinning, the stream
threshold, background decode (cursor hold, stop while decoding, destruction
with queued decodes) and ducking; run with and without `--sanitize`.
Not verified on a device: decode latency on phone storage and the audible
effect of the default thresholds.

## PCM streams (video soundtracks)

`Mixer::stream()` returns a `PcmStream`: 48 kHz stereo float pushed by one producer thread
(the video demuxer) through a one-second ring buffer and pulled by miniaudio as a non-spatial
sound in the master group, so background suspension, fades and ducking apply while game sound
slots (`stop(0)`, pitch, categories) do not touch it. `played()` counts frames handed to the mixer
and is the video's master clock; underruns play silence and are not counted. `SoundService`
exposes it as `VSoundService::CreatePcmStream()`. Covered by `tests/audio/service.cpp`.
