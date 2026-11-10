# FFmpeg for in-game video

`build.py` builds the minimal FFmpeg used by `src/libs/video` from the pinned source
(`tools/android/engine_sources.py`: FFmpeg n8.1.3, commit `1041abdc962f`). CMake runs it at
configure time (`cmake/ffmpeg.cmake`); `tests/video/run.py` runs it for the host check.

## Cache

A build is stored under `<cache>/<target>-<key>/` (`include/`, `lib/`, `configure.txt`,
`COPYING.LGPLv2.1`). The key hashes the source revision, the configure flags, the compiler's
`--version` and `BUILD_FORMAT` in `build.py`; any change builds a new prefix, an unchanged one is reused
by every build directory and worktree pointing at the same cache. The default cache is
`<prepared sources>/../ffmpeg-cache` (e.g. `build/android-ci/ffmpeg-cache`); override with
`-DSTORM_FFMPEG_CACHE=`. A lock file serialises concurrent builds of one target. Compiles go
through `ccache` when it is installed (the launcher is printed and is not part of the key). A
cold build takes about two minutes per target on the development host.

## Licence

LGPL-2.1-or-later only: no `--enable-gpl`, `--enable-version3` or `--enable-nonfree`.
`build.py` fails unless the configured `FFMPEG_LICENSE` is "LGPL version 2.1 or later". The
libraries ship as unmodified shared objects so that they can be replaced. Notice for the APK:
`third_party/licenses/FFmpeg-LGPL-2.1.txt`; the build's `configure.txt` is packaged next to it
as `FFmpeg-configure.txt`.

## Configure flags

Common to every target:

```
--disable-everything --disable-autodetect --disable-programs --disable-doc --disable-network
--disable-avdevice --disable-avfilter --disable-debug
--enable-shared --disable-static --enable-pic
--enable-avcodec --enable-avformat --enable-swscale --enable-swresample --enable-pthreads
--enable-demuxer=asf,matroska
--enable-decoder=wmv3,vc1,wmav2,vp8,vp9,vorbis,opus
```

- `asf` + `wmv3`/`vc1` + `wmav2`: TEHO (`RESOURCE/VIDEOS/*.wmv`, WMV3 640x480, WMA2).
- `matroska` (includes WebM) + `vp9` + `vorbis`: Caribbean Legend 2.0 (`Resource/Videos/*.webm`,
  VP9 1920x1080, Vorbis 48 kHz). `vp8` and `opus` are the other WebM codecs.
- Required parsers and bitstream filters (e.g. `vp9_superframe_split`) are pulled in by the
  decoders' own dependencies. No protocols: the player reads through its own I/O callbacks.

`android-arm64` adds:

```
--enable-cross-compile --target-os=android --arch=aarch64 --cpu=armv8-a
--enable-jni --enable-mediacodec --enable-decoder=vp8_mediacodec,vp9_mediacodec
--extra-ldflags=-Wl,-z,max-page-size=16384
```

MediaCodec gives hardware VP8/VP9 decoding (policy: `src/libs/video/README.md`); the
libraries link `libmediandk.so` and `libandroid.so` from the platform. The NEON assembly is on.
The 16 KiB page alignment is required of every library in the APK.

`host` adds `--disable-x86asm` (no nasm on the development hosts; the host build only serves
tests and the desktop development build).

## Size (arm64-v8a, stripped by `make install`)

| library | bytes |
| --- | --- |
| libavcodec.so | 2,834,608 |
| libswscale.so | 1,152,744 |
| libavutil.so | 722,240 |
| libavformat.so | 333,408 |
| libswresample.so | 97,112 |
| total | 5,140,112 |
