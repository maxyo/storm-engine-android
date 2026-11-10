#!/usr/bin/env python3
"""Summarize ff_keys.json manifests (from engine.ini ff_key_log=1) as markdown.

  summarize_keys.py CAPTURE_ROOT [--top 30]

Reads every <scene>/<run>/ff_keys.json, merges keys across runs, and prints unique key
counts overall and per scene tag, the top keys by draw count, the shader hashes with their
technique names, and the per-frame counter maxima (S3) against bgfx limits.
"""
import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

BGFX_LIMITS = {'draws': 65536, 'set_render_target': 256}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('--top', type=int, default=30)
    a = p.parse_args()
    overall, per_tag, s3 = Counter(), defaultdict(Counter), defaultdict(dict)
    shaders = defaultdict(lambda: [None, Counter()])
    for f in sorted(a.root.glob('*/*/ff_keys.json')):
        try:
            d = json.loads(f.read_text())
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        for tag, t in d['tags'].items():
            for k in t['keys']:
                overall[k['key']] += k['count']
                per_tag[tag][k['key']] += k['count']
            for metric, v in t['s3'].items():
                cur = s3[tag].get(metric, {'max': 0, 'p95': 0})
                s3[tag][metric] = {'max': max(cur['max'], v['max']), 'p95': max(cur['p95'], v['p95'])}
        for sh in d['shaders']:
            shaders[sh['hash']][0] = sh['kind']
            shaders[sh['hash']][1].update(sh['techniques'])
    print('Unique FF keys overall: %d\n' % len(overall))
    print('| scene tag | unique keys | draws (all frames) |\n| --- | --- | --- |')
    for tag in sorted(per_tag):
        print('| %s | %d | %d |' % (tag, len(per_tag[tag]), sum(per_tag[tag].values())))
    print('\n## Top %d keys by draw count\n\n| # | draws | key |\n| --- | --- | --- |' % a.top)
    for i, (k, c) in enumerate(overall.most_common(a.top), 1):
        print('| %d | %d | `%s` |' % (i, c, k))
    print('\n## Shaders seen\n\n| hash | kind | techniques (draws) |\n| --- | --- | --- |')
    for h, (kind, techs) in sorted(shaders.items()):
        print('| %s | %s | %s |' % (h, kind, ', '.join('%s (%d)' % x for x in techs.most_common())))
    print('\n## Per-frame counters (max / p95 over capture window)\n')
    metrics = ['draws', 'up_bytes', 'set_render_target', 'clears', 'vb_ib_locks', 'locked_bytes', 'distinct_textures']
    print('| scene tag | ' + ' | '.join(metrics) + ' |\n| --- |' + ' --- |' * len(metrics))
    for tag in sorted(s3):
        print('| %s | ' % tag + ' | '.join('%d / %d' % (s3[tag][m]['max'], s3[tag][m]['p95']) for m in metrics) + ' |')
    peak = {m: max((s3[t][m]['max'] for t in s3), default=0) for m in metrics}
    print('\nPeak draws/frame %d vs bgfx 64K limit; peak SetRenderTarget/frame %d vs 256 views.'
          % (peak['draws'], peak['set_render_target']))


if __name__ == '__main__':
    main()
