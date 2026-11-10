#!/usr/bin/env python3
"""Synthetic audio_import checks (FSB5/bank/WAV/string table -> Ogg Vorbis,
decoded with stb_vorbis). No game data.

Optional, local only: --game-dir <game root> --out <dir> also remuxes every
Resource/Sounds/Banks/**.bank, decodes all outputs with stb_vorbis against the
bank manifests and converts up to --wav-per-format WAVs of each WAV format.
Outputs stay in --out (never in the repository).
"""
import argparse
import collections
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path)
p.add_argument('--game-dir', type=Path)
p.add_argument('--out', type=Path)
p.add_argument('--wav-per-format', type=int, default=2)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
build = (a.build_dir or root / 'build/audio-import-check').resolve()
if a.game_dir and not a.out:
    p.error('--game-dir needs --out')


def timed(label, function):
    started = time.monotonic()
    result = function()
    print(f'{label}: {time.monotonic() - started:.2f}s', flush=True)
    return result


def configure_and_build():
    command = ['cmake', '-S', str(Path(__file__).parent), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release']
    if shutil.which('ninja'):
        command += ['-G', 'Ninja']
    if not (build / 'CMakeCache.txt').exists():
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    result = subprocess.run(['cmake', '--build', str(build), '--parallel', str(os.cpu_count())],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise SystemExit(result.stdout[-20000:] + '\nbuild failed')


timed('build', configure_and_build)
check = build / 'audio-import-check'
tool = build / 'storm-audio-import'
timed('synthetic checks', lambda: subprocess.run([str(check)], check=True))
if not a.game_dir:
    raise SystemExit(0)

out = a.out.resolve()
banks_out = out / 'banks'
shutil.rmtree(banks_out, ignore_errors=True)
banks = a.game_dir / 'Resource/Sounds/Banks'
scan = timed('bank scan + remux', lambda: subprocess.run([str(tool), 'scan', str(banks), str(banks_out)],
                                                           capture_output=True, text=True))
print(scan.stdout.strip().splitlines()[-1], flush=True)
if scan.stderr:
    print(scan.stderr, flush=True)

expected = {}
for manifest in banks_out.rglob('manifest.json'):
    for sample in json.loads(manifest.read_text())['samples']:
        if sample['status'] == 'ok':
            expected[str(manifest.parent / sample['file'])] = (sample['frames'], sample['channels'], sample['rate'])


def decode(files):
    lines = subprocess.run([str(check), '--decode', *files], check=True, capture_output=True, text=True).stdout
    return {line.split(' ', 3)[3]: tuple(map(int, line.split(' ', 3)[:3])) for line in lines.splitlines()}


decoded = timed('stb_vorbis decode of all remuxed samples', lambda: decode(sorted(expected)))
mismatch = [f for f in expected if decoded.get(f) != expected[f]]
print(f'decoded {len(decoded)} remuxed files; frames/channels/rate equal to FSB5: {len(decoded) - len(mismatch)}')
for f in mismatch[:20]:
    print('  mismatch', f, 'decoded', decoded.get(f), 'expected', expected[f])


def wav_format(path):
    with path.open('rb') as f:
        data = f.read(4096)
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return ('not RIFF/WAVE',)
    offset = 12
    while offset + 8 <= len(data):
        chunk, size = data[offset:offset + 4], struct.unpack_from('<I', data, offset + 4)[0]
        if chunk == b'fmt ':
            return struct.unpack_from('<HHI', data, offset + 8) + struct.unpack_from('<H', data, offset + 22)
        offset += 8 + size + (size & 1)
    return ('no fmt in first 4 KiB',)


groups = collections.defaultdict(list)
for path in sorted((a.game_dir / 'Resource/Sounds').rglob('*')):
    if path.suffix.lower() == '.wav':
        groups[wav_format(path)].append(path)
wav_out = out / 'wav'
shutil.rmtree(wav_out, ignore_errors=True)
wav_out.mkdir(parents=True)
picked = [path for group in groups.values() for path in group[:a.wav_per_format]]


def convert():
    results = {}
    for index, path in enumerate(picked):
        target = wav_out / f'{index:03d}_{path.stem}.ogg'
        run = subprocess.run([str(tool), 'wav', str(path), str(target)], capture_output=True, text=True)
        results[path] = (target, run.returncode, run.stderr.strip())
    return results


converted = timed(f'WAV encode of {len(picked)} files ({len(groups)} formats)', convert)
ok = {path: target for path, (target, code, _) in converted.items() if code == 0}
wav_decoded = decode([str(t) for t in ok.values()]) if ok else {}
print(f'WAV: {len(picked)} picked, {len(ok)} encoded, {sum(1 for t in ok.values() if wav_decoded.get(str(t), (0,))[0] > 0)} decode')
for path, (_, code, error) in converted.items():
    if code:
        print('  failed', path.relative_to(a.game_dir), error)
failed = len(mismatch) + (scan.returncode != 0) + (len(ok) != len(picked))
raise SystemExit(1 if failed else 0)
