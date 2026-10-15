#!/usr/bin/env python3
"""Isolated synthetic audio checks; no game data, SDK download or terms acceptance."""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--build-dir', type=pathlib.Path, default=ROOT/'build/audio')
p.add_argument('--cxx', default='g++')
p.add_argument('--compile-only', action='store_true')
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
if not a.compile_only and shutil.which("ffmpeg") is None:
    raise SystemExit("Host audio tests require ffmpeg with libmp3lame and libvorbis encoders.")
build = a.build_dir.resolve()
sources = build/'sources'
sources.mkdir(parents=True, exist_ok=True)
inputs = {
    'miniaudio.h': ('https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h', '7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6'),
    'stb_vorbis.c': ('https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c', '4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636'),
}
for name, (url, digest) in inputs.items():
    target = sources/name
    if not target.exists():
        with urllib.request.urlopen(url) as response:
            target.write_bytes(response.read())
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
        raise SystemExit(f'checksum mismatch: {target}')

def run(cmd):
    subprocess.run(list(map(str, cmd)), check=True)

# Preserve original hashes/notices and patch a separate compilation copy.
patched = build/'patched'
patched.mkdir(exist_ok=True)
for name in inputs:
    shutil.copyfile(sources/name, patched/name)
run(['patch', '-p1', '-i', ROOT/'tools/android/patches/miniaudio-0.11.23-error-cleanup.patch', '-d', patched])

flags = ['-std=c++17', '-O1', '-g', '-pthread', '-I'+str(patched), '-I'+str(ROOT/'android/audio')]
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
run([a.cxx, *flags, '-c', ROOT/'android/audio/implementation.cpp', '-o', build/'implementation.o'])
run([a.cxx, *flags, build/'implementation.o', ROOT/'tests/audio/probe.cpp', '-Wl,--no-undefined', '-ldl', '-lm', '-o', build/'audio-probe'])
run([a.cxx, *flags, build/'implementation.o', ROOT/'android/audio/service_backend.cpp', ROOT/'tests/audio/service.cpp', '-Wl,--no-undefined', '-ldl', '-lm', '-o', build/'audio-service-check'])
if a.compile_only:
    print('Audio probe linked only; target playback and tests NOT executed.', flush=True)
else:
    fixtures = build/'fixtures'
    fixtures.mkdir(exist_ok=True)
    # All codec files derive from our generated mono sine wave.
    run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=3', '-ac', '1', '-c:a', 'pcm_s16le', fixtures/'tone.wav'])
    for ext, codec in [('mp3','libmp3lame'),('ogg','libvorbis')]:
        run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-i', fixtures/'tone.wav', '-c:a', codec, fixtures/('tone.'+ext)])
    for ext, codec in [('wav','pcm_s16le'),('mp3','libmp3lame'),('ogg','libvorbis')]:
        run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-i', fixtures/'tone.wav', '-ar', '44100', '-c:a', codec, fixtures/('tone441.'+ext)])
    run([build/'audio-probe', fixtures])
    run([build/'audio-service-check', fixtures])

    long_fixtures = build/'long-fixtures'
    long_fixtures.mkdir(exist_ok=True)
    for ext, codec in [('mp3','libmp3lame'),('ogg','libvorbis')]:
        run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=180', '-ac', '1', '-c:a', codec, long_fixtures/('tone.'+ext)])
    peaks = []
    for directory in [fixtures, long_fixtures]:
        output = subprocess.check_output([str(build/'audio-probe'), str(directory), '--stream-memory'], text=True)
        peaks.append(int(output.strip().split('=')[1]))
    if peaks[1] - peaks[0] > 8192:
        raise SystemExit(f'Streaming memory grew by more than 8 MiB: {peaks}')
    print(f'Two-stream peak RSS KiB (3s, 180s): {peaks}; growth <= 8 MiB PASS')
