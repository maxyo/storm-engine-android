# Open audio backend decision brief

Scope: inspected Storm at `60f045d0e42faba69204ee7420f7a7cb21cb3e2f`.
This document retains the original decision brief and subsequent implementation
checkpoints. The current SoundService integration is described at the end.

## Recommendation

Prototype **miniaudio 0.11.23 plus a pinned stb_vorbis decoder** behind a small
engine-owned audio interface. Keep `VSoundService`, aliases, schemes, stamped
sound IDs, category volumes and two music slots. Do not emulate the whole FMOD
API or replace the service with no-op functions. Keep the desktop FMOD path
selectable until behavioral comparisons pass.

Evidence: the official miniaudio 0.11.23 implementation compiled and linked with
NDK r30/API24 into an AArch64 shared library using `--no-undefined`. No device
playback, Vorbis integration, codec corpus, or performance test has run yet.

## Actual engine requirements

FMOD types and calls are concentrated in `sound_service.cpp` (1637 lines) and
`sound_service.h` (223 lines). Build changes touch its CMake target and Conan
recipe. Public game/script-facing `VSoundService` need not change.

| Existing behavior / API | Replacement work |
| --- | --- |
| System create/init/update/close; auto output | Real miniaudio engine/device; report backend errors and prohibit silent null-backend fallback on Android |
| Cached `createSound`, paused `playSound`, independent channels | Shared decoded resource plus per-play sound instances and stamped handle lifetime |
| `createStream` for MP3_STEREO; two alternating music slots | Streaming file source, seek/cursor in milliseconds, loop and two-slot crossfade |
| `setVolume`, category gains, pitch, pause, stop | Map sound/group controls; preserve game-thread fader semantics |
| PCM_3D position, listener position/forward/up, linear min/max rolloff | Match units, coordinate handedness, mono/stereo policy and attenuation with measured PCM tests |
| Priority 0..255, 4095 logical slots, `isVirtual` | Engine-owned priority/voice budget and virtualization policy; no equivalent FMOD contract assumed |
| DSP clock, fade points, channel-group delay, mixer suspend/resume | Audio-frame clock fades and lifecycle state machine; avoid stopping device before fade completes |
| Debug CPU/audibility/virtual/position metrics | Report meaningful backend metrics, mark unsupported metrics unavailable rather than invent numbers |

Music uses the MP3_STEREO enum but names/storage use OGG terminology: codec
requirements cannot be inferred from enum names. WAV, MP3 and Ogg Vorbis should
be the initial test set; actual game corpus is not available. No Studio banks,
FMOD DSP/reverb graph or network stream usage was found in the service.
Velocity is currently zero and distance factor is 1.0; Doppler is not an initial
parity target. Do not accidentally switch coordinate conventions.

Existing issues must be separated from port regressions: SoundDuplicate returns
0, SoundRestart/SoundRelease/SetEnabled are effectively empty, and
SoundGetPosition reads FMOD's cursor but returns 0. MP3_3D has no explicit 3D
configuration like PCM_3D. Zero-time fades and zero min/max distances need
characterization. Do not silently expand this port into fixing script semantics.

## Candidate tradeoffs

- **miniaudio:** engine mixing, spatialization, decoded resources, streaming,
  pitch and frame-based fades; Android AAudio and OpenSL ES fallback. Built-in
  WAV/MP3/FLAC, with separately enabled Vorbis integration. It lacks an assumed
  drop-in FMOD voice virtualization/priority contract, which is the largest
  adaptation risk. Choose its MIT-0 alternative and preserve notices anyway.
- **OpenAL Soft:** mature 3D source/listener model, LGPL-2.0-or-later; plausible
  with GPLv3 when its distribution obligations are met. Requires separate
  decoding, stream queue/refill workers, music fades and voice management.
  More integration work here; HRTF is not an existing requirement. Optional
  HRTF data has separate licensing. Not the first prototype.

stb_vorbis offers MIT/public-domain alternatives; choose MIT and retain its
notice. It does not support old Vorbis floor-0 or chained streams, so validate
against the user's corpus before declaring codec compatibility. If required,
consider libvorbis/libogg with their own pinned versions/notices rather than
silently skipping files.

## Bounded first implementation for approval

1. Add only an isolated audio backend/probe target and pinned source provenance.
   No whole-service replacement yet. Output real PCM offline on host and a real
   audio device on Android; forbid null-device fallback for audible tests.
2. Prove WAV/MP3/Vorbis decode, concurrent 2D/3D playback, loop/seek and two music
   streams using generated or properly licensed fixtures. Offline assertions:
   nonzero finite PCM, left/right orientation, attenuation, pitch, cursor,
   fade endpoint and bounded streaming memory. No game assets in repo.
3. Add explicit stop/start on lifecycle transitions, verify position and cleanup
   across 100 cycles off-device, then audibly test Home/resume, lock/unlock and
   output-route changes on an authorized device.
4. Stop for review with measurements. Only then adapt SoundService and implement
   a tested logical/physical voice policy. Initial budget such as 64/128 physical
   voices is a measured tuning candidate, not a promise to mix 4095 voices on a
   phone. Include overflow priority ordering and stale-handle tests.

Planning estimate, not a measured schedule: prototype is a small separate target
with roughly 300–600 lines of adapter/probe and focused tests. Full service
adaptation is medium-sized: approximately 800–1500 lines changed/added plus
voice/lifetime/lifecycle tests, followed by device/game parity testing. Shared
logic remains; avoid rewriting the roughly 1900-line module wholesale. Device
and real-resource findings can expand scope.

## Upstream permission check

Tracked license/readme files, the complete LICENSE.txt history (one addition),
source text and targeted upstream issue searches for license/FMOD GPL/exception
were checked. No FMOD linking exception or specific redistribution permission
was found. The official engine publication announcement is evidence of engine
source publication permission, not evidence of a separate FMOD grant. This is
not proof that no private or unindexed permission exists.

GPLv3 section 7 allows additional permissions; none specific to FMOD was found
in the checked material. The current FMOD EULA clauses 1.2 and 1.3 restrict
engine/toolset distribution and SDK redistribution. These facts leave the
combined-binary distribution question unresolved; they do not justify declaring
all private use or every possible licensed distribution automatically illegal.
A rights-holder-specific written grant could change the conclusion. No terms
were accepted and no proprietary SDK files were redistributed.

## Primary sources

- [Storm sound implementation](https://github.com/storm-devs/storm-engine/blob/a68f43df197a50c5177f40209abc109f01f4b114/src/libs/sound_service/src/sound_service.cpp)
- [Storm GPLv3 text](https://github.com/storm-devs/storm-engine/blob/develop/LICENSE.txt)
- [Official source publication announcement](https://store.steampowered.com/news/app/223330/view/3013444995188538670)
- [miniaudio 0.11.23 source](https://github.com/mackron/miniaudio/blob/0.11.23/miniaudio.h) and [license](https://github.com/mackron/miniaudio/blob/0.11.23/LICENSE)
- [stb_vorbis source, limitations and license](https://github.com/nothings/stb/blob/master/stb_vorbis.c)
- [OpenAL Soft project and license statement](https://github.com/kcat/openal-soft)
- [FMOD current EULA](https://www.fmod.com/legal)

## Isolated implementation checkpoint

`android/audio/` and `tests/audio/` now implement the bounded experiment with
miniaudio 0.11.23 and stb_vorbis pinned to
`2c980bb59875b0d32144a71867fbdebb2f77cd20`. Source hashes and license choices are
recorded in the experiment README. Generated sine-wave WAV/MP3/Vorbis fixtures
stay in ignored build directories. The existing integration-probe APK is
unchanged and does not contain this audio experiment.

Integration assessment from the experiment and current SoundService source:

- ASan found a use-after-free in miniaudio 0.11.23 when loading a missing file.
  The pinned build applies a narrow cleanup-order patch with a regression case.
  Do not use the unpatched pin for the adapter's error paths.
- Streaming Vorbis may report unknown length and a cumulative loop cursor.
  Music position persistence must define its own duration/modulo policy rather
  than assume that every codec returns a wrapped cursor.
- WAV/MP3/Vorbis, per-instance controls, two music streams and 3D mixing have a
  usable API mapping. Streaming seek is asynchronous: the mixer consumes a
  request and a worker refills pages. A service adapter must expose/handle this
  transition and avoid pretending a returned call means seek has completed.
- Offline engine stop/start cannot represent device lifecycle directly.
  Offline tests pause the manual PCM pump; real device mode uses engine stop/
  start and explicitly limits Android backends to AAudio/OpenSL ES.
- Keep `VSoundService`, alias/scheme logic, category volumes, cache names,
  stamped handles and two OGG slots. Replace backend resource/channel fields
  behind a separate interface rather than making fake FMOD types.
- Next adapter needs an explicit physical-voice budget, priority ordering and
  logical voice state/cursor policy. Test overflow and stale IDs before mapping
  the current 4095 logical slots; `isVirtual` has no assumed equivalent.
- Existing `SetActiveWithFade` schedules DSP-clock fades and immediately
  suspends the mixer. The replacement needs a state machine that completes the
  fade before stopping the device, including reversal during rapid resume.
  Prototype fade tests do not establish parity with that service behavior.
- Stereo 3D sounds, engine listener handedness, zero distance/fade cases,
  resource cache sharing and real game corpus compatibility remain integration
  tests. The synthetic mono spatial test establishes only its documented
  left/right and distance behavior.

Full SoundService replacement and Android engine linking are not completed by
this checkpoint. Device playback, Home/lock/route changes and performance must
be measured before any game-ready prerelease claim. No FMOD download is needed
to continue the open backend, but no device or game corpus is available here.

## SoundService integration (work after prototype approval)

Android now selects `STORM_OPEN_AUDIO`; desktop defaults to its original FMOD
implementation. `sound_service_common.cpp` retains the 15 extracted
alias/scheme/debug-helper functions byte-for-byte. `sound_service_open.cpp`
connects the game's real service entry points to `android/audio/service_backend`.
The prototype is therefore used by the engine target, not substituted for it.

The manager owns stamped logical IDs, two fixed music slots, category gains,
shared decoded caches, streaming duration indexes, fades, priority selection and
virtual cursor progression. Synthetic checks cover priority overflow, stale
handles, pause/cache/resume, lifecycle, music handoff and category gain updates.
The replacement policy and intentional retained legacy behaviors are documented
in `android/game/TESTING.md`. No FMOD SDK or headers are needed on Android.

Host checks do not establish FMOD parity for a real resource corpus. No claim is
made about all device routes, stereo spatial policy, GPU compatibility, voice
budgets on phones or gameplay. Physical Android validation is the next gate.
