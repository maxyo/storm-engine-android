#!/usr/bin/env python3
"""Decode synthetic clips through src/libs/video with the engine's own FFmpeg build.

Clips are generated here with the system ffmpeg (red then blue picture, 440 Hz tone), never
committed: WebM VP9+Vorbis (Caribbean Legend's format), WebM VP8+Opus, ASF+WMAv2 (TEHO's
container and audio codec; FFmpeg has no WMV3 encoder, so the picture is VP8) and a silent
WebM VP9 that plays on the wall clock. --smoke plays local media files (e.g. a TEHO .wmv or a
Caribbean Legend .webm) in real time and reports what was shown.

  python3 tests/video/run.py --dependency-sources build/host-ci/sources [--smoke FILE ...]
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--build-dir', type=Path, default=Path(__file__).resolve().parents[2] / 'build/video-check')
p.add_argument('--smoke', type=Path, nargs='*', default=[])
p.add_argument('--smoke-seconds', type=float, default=10)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
if shutil.which('ffmpeg') is None:
    sys.exit('tests/video needs the system ffmpeg (libvpx, libvorbis, libopus) to generate its clips')

sources = a.dependency_sources.resolve()
prefix = Path(subprocess.check_output([sys.executable, str(root / 'tools/ffmpeg/build.py'), '--source', str(sources / 'ffmpeg'),
                                       '--cache', str(sources.parent / 'ffmpeg-cache'), '--target', 'host'], text=True).strip())
binary = build / 'video-check'
cxx = os.environ.get('CXX', 'c++')
subprocess.run([cxx, '-std=c++20', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-I' + str(root / 'src/libs/video/include'), '-isystem', str(prefix / 'include'),
                str(root / 'src/libs/video/src/player.cpp'), str(Path(__file__).with_name('check.cpp')),
                '-L' + str(prefix / 'lib'), '-Wl,-rpath,' + str(prefix / 'lib'),
                '-lavformat', '-lavcodec', '-lswscale', '-lswresample', '-lavutil', '-o', str(binary)], check=True)

picture = 'color=c=red:s=160x120:r=10:d={half}[a];color=c=blue:s=160x120:r=10:d={half}[b];[a][b]concat=n=2:v=1:a=0'
clips = [  # name, video codec, audio codec, seconds
    ('vp9-vorbis.webm', ['-c:v', 'libvpx-vp9', '-deadline', 'realtime', '-cpu-used', '8'], ['-c:a', 'libvorbis'], 2.0),
    ('vp8-opus.webm', ['-c:v', 'libvpx', '-deadline', 'realtime'], ['-c:a', 'libopus'], 2.0),
    ('vp8-wmav2.asf', ['-c:v', 'libvpx', '-deadline', 'realtime'], ['-c:a', 'wmav2', '-b:a', '64k'], 2.0),
    ('vp9-silent.webm', ['-c:v', 'libvpx-vp9', '-deadline', 'realtime', '-cpu-used', '8'], None, 1.0),
]
for name, video, audio, seconds in clips:
    clip = build / name
    if not clip.exists():
        command = ['ffmpeg', '-nostdin', '-v', 'error', '-y', '-filter_complex', picture.format(half=seconds / 2)]
        if audio:
            command += ['-f', 'lavfi', '-i', f'sine=frequency=440:sample_rate=44100:duration={seconds}', *audio, '-ac', '2']
        command += [*video, '-pix_fmt', 'yuv420p', str(clip)]
        subprocess.run(command, check=True)
    subprocess.run([str(binary), 'clip', str(clip), '160', '120', str(seconds), '1' if audio else '0'], check=True, timeout=120)
for media in a.smoke:
    subprocess.run([str(binary), 'smoke', str(media), str(a.smoke_seconds)], check=True, timeout=a.smoke_seconds + 60)
print('PASS: video decode (VP9/Vorbis, VP8/Opus, ASF/WMAv2, silent), audio clock, wall clock, pause', flush=True)
