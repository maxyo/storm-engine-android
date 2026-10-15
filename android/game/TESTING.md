# Full engine Android bring-up build

This APK contains the real ARM64 engine, DXVK D3D9, SDL and open audio
SoundService. It is not the earlier integration probe. No game resources are
bundled. Installation, Vulkan presentation, device audio, lifecycle recovery
and gameplay have not yet been validated on a physical device. The build is
an Actions artifact; game-ready prerelease remains disabled until that evidence
exists. Debug signing is for testing only; clean CI runs use different keys.

## Minimum external inputs

- An ARM64 Android device, API 24 or newer, with Vulkan support compatible with
  this pinned DXVK Native renderer. Vulkan 1.0 in the manifest is only the
  loader baseline, not a promise that the GPU meets DXVK's complete requirements.
- Legally obtained TEHO installation files, including `engine.ini`, scripts and
  `resource` content. Put `engine.ini` at the ZIP root. No copyrighted files need
  to be uploaded to GitHub or sent to the developer.
- Enough app storage for the extracted installation plus the ZIP. Import is
  bounded to 32 GiB and 200,000 files. Keep the app open during import. Failed
  imports can leave completed files; retry a complete archive.
- Prefer USB debugging and adb for logs. No credentials, FMOD SDK or external
  storage permission are required.

The launcher imports a user-selected ZIP using Android's document picker.
Resources go to `getExternalFilesDir()/game` (internal files fallback). Saves,
logs and screenshots use internal `files/storm`. The exact game path is shown
in the launcher. Back up saves before uninstalling or changing debug signing
keys. Existing engine resources under `src/techniques` and
`src/libs/shared_headers/include/shared` may need to be overlaid into the
installation's `resource/techniques` and `resource/shared` for this engine
revision, as with upstream desktop installation; keep a backup of game data.

## Device acceptance sequence

1. Install `storm-engine-arm64-debug.apk`. Open the launcher without resources;
   verify that Start reports missing `engine.ini`, rather than entering a probe.
2. Import the local game ZIP. Verify missing/corrupt ZIP handling and an archive
   containing `../` paths is rejected. Check available storage and import paths.
3. Start engine; collect complete `adb logcat` and app logs. Confirm native
   library loading, Vulkan device/presentation and actual game menu rendering.
4. Exercise a WAV effect, MP3/Vorbis music, two-slot music changes, category
   volumes, positional sounds, repeated scene changes and priority pressure.
   `engine.ini` `[sound] physical_voices=64` is the default; 2..128 is accepted.
5. Test Home/resume, lock/unlock, orientation and headset/Bluetooth route changes
   during music and fades. Check that sound stops in background, cursors resume,
   no controls remain held and rendering recovers without a deadlock.
6. Test touch plus a physical keyboard/mouse or controller. Overlay buttons map
   W/A/S/D, Q/E, Shift, Space, Esc, F1/F2/F3, Enter, Tab, R and left/right mouse.
   Drag Look for relative mouse motion; the uncovered SDL surface provides
   ordinary touch/mouse input. Key labels are literal bindings; game actions
   depend on the user's configuration. This is a basic test layout, not a
   finished mobile control scheme or a claim that all gameplay is reachable.
7. Load a scene, move, interact, enter combat, save, restart and reload. Compare
   audio, script behavior and save contents against the desktop engine.

Useful capture commands (package is separate from the probe):

```
adb logcat -c
adb shell am start -n org.stormengine.port.game/org.stormengine.port.GameLauncherActivity
adb logcat -d > storm-device-logcat.txt
adb shell run-as org.stormengine.port.game ls files/storm
```

## Known behavior and limits

The service shares aliases/schemes and retains the game's public call interface.
4095 logical slots include two fixed music IDs. Up to 64 physical voices are
selected by priority (0 highest), then estimated audibility and creation order.
Virtual voices advance on the audio frame clock, pause in background, and seek
back to their logical cursor when promoted. This is an explicit replacement
policy, not a claim of bit-identical FMOD virtualization.

WAV/MP3/Vorbis synthetic decode/control tests, ASan/UBSan, overflow/stale-ID tests,
100 offline lifecycle cycles and NDK linking are build evidence only. No real
game audio corpus was available. Vorbis floor-0/chained streams remain unsupported.
Streaming seeks complete asynchronously. File-backed stream lengths are indexed
so looping music positions persist modulo duration without decoding whole music
tracks into RAM. Cached effects still use shared decoded resources.

Legacy no-op SoundRestart/SoundRelease/SetEnabled and zero SoundDuplicate/
SoundGetPosition returns are retained. Legacy MP3_3D remains non-spatial. Invalid
zero/negative distance ranges use finite open-backend defaults (1..10000 units).
The category-volume compounding bug and zero-time fade division are avoided in
the new backend. Debug CPU/memory telemetry is explicitly unavailable.

Device suspend is paired with resume on the game/control thread. Fade-out waits
for the audio-frame fade, bounded to 1 second (1.2 second timeout for a stalled
device), before stopping output. That can delay the background transition; it
never sleeps in the audio callback. Frame/route behavior still needs device tests.
Android sea calculations use a sequential standard algorithm because this NDK's
libc++ has no parallel execution policies. Performance remains unmeasured.
