# Isolated audio experiment (not the game)

This deliberately does not replace SoundService or change desktop FMOD. The
small RAII adapter uses real miniaudio mixing/decoding, either offline PCM or an
explicit real-device backend list (Android AAudio/OpenSL ES; no null fallback).
No SDK terms or FMOD license are accepted by this code.

Sources are downloaded by `tests/audio/run.py` into the ignored build directory
and verified by SHA256 before compilation:

- miniaudio 0.11.23, official `mackron/miniaudio` tag; SHA256
  `7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6`.
  Choose the MIT-0 alternative in its embedded license.
- stb_vorbis, official `nothings/stb` commit
  `2c980bb59875b0d32144a71867fbdebb2f77cd20`; SHA256
  `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`.
  Choose the MIT alternative in its embedded license. Retain that notice when
  distributing binaries. This experiment does not package an APK.

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
