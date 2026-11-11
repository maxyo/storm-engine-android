# video

FFmpeg-based playback for the script-facing `CAviPlayer` entity (`xinterface/aviplayer`) and the
`VIDEO` interface node (`xi_video_rect`, which embeds a `CAviPlayer`). The FFmpeg build is
described in `tools/ffmpeg/README.md`.

## Pipeline

- Demux thread: reads packets through `Source` (the engine passes the file resolved by the file
  service), queues video packets for the video thread, and decodes audio itself; audio is
  converted by swresample to 48 kHz stereo float and written to the `AudioSink`. The sink's
  one-second buffer paces demuxing to real time.
- Video thread: decodes and converts each picture with swscale (BT.601/BT.709 and range from
  the stream) into an RGBA frame no larger than the screen (scaled down only), into a queue of
  three frames.
- Game thread (`CAviPlayer::Realize`): `update()` advances the clock and shows the newest frame
  that is due (late frames are dropped), the frame is copied into an `A8B8G8R8` texture through
  the renderer's texture ids (`TextureCreate`, `TextureLock`/`TextureUnlock`) and drawn
  letterboxed with the `battle_icons` technique (texture colour, no vertex colour).

RGBA conversion happens in swscale on the decoder thread rather than in a YUV shader: the
renderer's shaders belong to `renderer_bgfx`, and `xi_video_rect` needs a plain RGB texture it
can draw with the game's own `iVideo` technique. A YUV texture path would save the conversion and
the larger upload for 1080p video; it needs a shader port in `renderer_bgfx`.

## Clock

The audio output's played-frame count is the master clock (`VSoundStream::PlayedFrames`; the
engine's miniaudio mixer pulls the stream in its master group). Without an audio stream, or once
the soundtrack has played out, the wall clock continues from the last audio time. Pause
(application in background or the app's modal pause, both reported by `AppEvents`) freezes the
clock and pauses the stream; the miniaudio device is stopped by the sound service anyway.

## Decoder policy

Fixed per codec in `kPolicy` (`src/player.cpp`) and logged for every video
(`Video: <file>: 640x480 wmv3 (software) -> ...`):

| codec | Android | host |
| --- | --- | --- |
| WMV3, VC-1 | software (FFmpeg) | software |
| VP9 | MediaCodec `vp9_mediacodec`, then software | software |
| VP8 | MediaCodec `vp8_mediacodec`, then software | software |
| WMA v2, Vorbis, Opus | software | software |

MediaCodec is looked up through the Java `MediaCodecList` (the Java VM comes from SDL); FFmpeg
skips software-only codecs (`isSoftwareOnly`, `OMX.google.*`), so "hardware" means a real
hardware decoder. If none can be opened, the player logs a warning
(`no hardware vp9 decoder on this device (...); decoding in software, large videos may drop
frames`) and decodes in software. This is the one deliberate fallback: refusing to play would
skip story videos outright, while FFmpeg's NEON VP9 decoder with frame threads is still
watchable on most arm64 phones (frames that are late are dropped against the audio clock, the
sound never stutters). A codec outside the table, a container that cannot be read, or a decode
error ends the video with an error in the log, and the scripts receive `ievntEndVideo` as if it
had ended.

## Script interface (unchanged)

- `CreateEntity(&aviVideoObj, "CAviPlayer")`, `SendMessage(&aviVideoObj, "ls", MSG_SET_VIDEO_PLAY,
  "<name>.wmv")`: plays `resource\videos\<name>` full screen, letterboxed.
- `MSG_SET_VIDEO_FLAGS` bit 0: loop (reopened at the end).
- `ievntEndVideo` is sent once when the video has played out (or could not be played); the
  scripts delete the entity in their handler. Deleting it earlier stops playback (skip).
- Tap to skip: a tap that starts while the video is shown sends `Control Activation` and
  `Control Deactivation` of `ICancel` (the Esc key on PC); the scripts' `IVideoBreak` decides
  whether the video may be skipped (TEHO keeps `Artefact`, `Ending`, `Invasion` and `Temple`
  unskippable).
- The soundtrack plays at the music volume.

## Tests

`tests/video/run.py` (host): generated WebM VP9+Vorbis, WebM VP8+Opus, ASF+WMAv2 and a silent
VP9 clip through the real pipeline with ASan/UBSan; `--smoke FILE...` plays local media (TEHO
`.wmv`, Caribbean Legend `.webm`) in real time. `tests/audio/run.py` covers the PCM stream.
