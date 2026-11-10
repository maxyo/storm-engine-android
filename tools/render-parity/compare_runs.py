#!/usr/bin/env python3
"""Compare two capture trees scene by scene and print a markdown SSIM table.

  compare_runs.py BASELINE_DIR CANDIDATE_DIR [--masks masks.json] [--heatmaps OUT_DIR]

Each tree has <scene>/<run>/shot<N>.png (as written by capture_scene.py). For every
scene the first run of each tree is used (or --run NAME); shots are paired by index.
masks.json maps scene names (or "*") to lists of [x, y, w, h] rectangles.
Pass the same directory twice with --runs run1 run2 to get the D3D9 noise floor.
"""
import argparse
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ssim import compare  # noqa: E402


def shots(run_dir):
    return sorted(run_dir.glob('shot*.png'))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('baseline', type=Path)
    p.add_argument('candidate', type=Path)
    p.add_argument('--runs', nargs=2, default=('run1', 'run1'), metavar=('BASE_RUN', 'CAND_RUN'))
    p.add_argument('--masks', type=Path)
    p.add_argument('--heatmaps', type=Path)
    p.add_argument('--fail-below', type=float, help='exit 1 if any scene min SSIM is below this')
    a = p.parse_args()
    masks = json.loads(a.masks.read_text()) if a.masks else {}
    rows, failed = [], False
    for scene_dir in sorted(d for d in a.baseline.iterdir() if d.is_dir()):
        scene = scene_dir.name
        base, cand = scene_dir / a.runs[0], a.candidate / scene / a.runs[1]
        if not base.is_dir() or not cand.is_dir():
            rows.append((scene, 'missing', '', '', ''))
            continue
        pairs = list(zip(shots(base), shots(cand)))
        if not pairs:
            rows.append((scene, 'no shots', '', '', ''))
            continue
        rects = [tuple(r) for r in masks.get('*', []) + masks.get(scene, [])]
        scores = []
        for i, (x, y) in enumerate(pairs):
            heat = None
            if a.heatmaps:
                a.heatmaps.mkdir(parents=True, exist_ok=True)
                heat = str(a.heatmaps / ('%s-%s.png' % (scene, x.stem)))
            scores.append(compare(x, y, rects, heat))
        lo = min(scores)
        failed |= a.fail_below is not None and lo < a.fail_below
        rows.append((scene, str(len(pairs)), '%.4f' % statistics.mean(scores), '%.4f' % lo,
                     ' '.join('%.3f' % s for s in scores)))
    print('| scene | pairs | mean SSIM | min SSIM | per shot |')
    print('| --- | --- | --- | --- | --- |')
    for r in rows:
        print('| ' + ' | '.join(r) + ' |')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
