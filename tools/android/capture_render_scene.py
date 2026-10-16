#!/usr/bin/env python3
"""Capture an already-open Android scene without modifying app/device state.

Enable render_metrics=1 in engine.ini before launching. Frames measure successful
Present cadence, not GPU execution time. Keep loading/warmup outside a measured
capture; retain raw logs and screenshots for visual validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--scene', required=True, help='Human description including camera/weather/save')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--apk', type=Path, help='Exact APK used; records its SHA256')
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error('--seconds must be 10..600')
    args.output.mkdir(parents=True, exist_ok=False)
    adb = [args.adb, '-s', args.serial]

    def capture(name, *command):
        result = subprocess.run(adb + list(command), capture_output=True, check=False)
        (args.output / name).write_bytes(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{command}: {result.stderr.decode(errors="replace")}')
        return result.stdout.decode(errors='replace').strip()

    capture('device-properties.txt', 'shell', 'getprop')
    capture('package.txt', 'shell', 'dumpsys', 'package', 'org.stormengine.port.game')
    capture('memory-before.txt', 'shell', 'dumpsys', 'meminfo', 'org.stormengine.port.game')
    capture('before.png', 'exec-out', 'screencap', '-p')
    metadata = {'scene': args.scene, 'duration_requested_seconds': args.seconds,
                'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                'metric_kind': 'successful Present cadence; not GPU timing',
                'apk_sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest() if args.apk else None}
    with (args.output / 'logcat.txt').open('wb') as output:
        log = subprocess.Popen(adb + ['logcat', '-v', 'threadtime', '-T', '1'], stdout=output,
                               stderr=subprocess.PIPE)
        try:
            time.sleep(args.seconds)
        finally:
            log.terminate()
            _, errors = log.communicate(timeout=10)
        if log.returncode not in (0, -15):
            raise RuntimeError(errors.decode(errors='replace'))
    capture('after.png', 'exec-out', 'screencap', '-p')
    capture('memory-after.txt', 'shell', 'dumpsys', 'meminfo', 'org.stormengine.port.game')
    records = []
    for line in (args.output / 'logcat.txt').read_text(errors='replace').splitlines():
        if 'RenderMetrics:' in line:
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line.split('RenderMetrics:', 1)[1]))
            records.append(fields)
    metadata['windows'] = records
    # Do not mislabel percentiles of window percentiles as a whole-run percentile.
    if records:
        frames = sum(int(r['frames']) for r in records)
        seconds = sum(float(r['seconds']) for r in records)
        metadata['reported_frames'] = frames
        metadata['reported_seconds'] = seconds
        metadata['reported_window_weighted_fps'] = frames / seconds
        metadata['window_p95_ms_range'] = [min(float(r['frame_ms_p95']) for r in records),
                                           max(float(r['frame_ms_p95']) for r in records)]
    metadata['note'] = ('Window boundaries may extend outside capture. Compare only matched scenes, '
                        'camera, resolution, weather, backend and warmup; review screenshots and errors.')
    (args.output / 'metrics.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Captured {len(records)} timing windows in {args.output}')


if __name__ == '__main__':
    main()
