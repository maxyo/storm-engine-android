#!/usr/bin/env python3
"""SSIM between two PNG screenshots (grayscale, 8x8 uniform window, stride 4).

  ssim.py a.png b.png [--mask x,y,w,h ...] [--heatmap diff.png] [--json]

Mask rectangles are excluded (both images filled with the same value), e.g. to drop
Android overlay widgets or the clock caption. Prints the mean SSIM.
"""
import argparse
import json
import sys

import numpy as np
from PIL import Image

C1 = (0.01 * 255) ** 2
C2 = (0.03 * 255) ** 2


def load_gray(path):
    img = Image.open(path).convert('RGB')
    rgb = np.asarray(img, dtype=np.float64)
    return rgb @ np.array([0.299, 0.587, 0.114])


def parse_rect(text):
    x, y, w, h = (int(v) for v in text.split(','))
    return x, y, w, h


def apply_masks(a, b, masks):
    keep = np.ones(a.shape, dtype=bool)
    for x, y, w, h in masks:
        keep[max(y, 0):y + h, max(x, 0):x + w] = False
    a = np.where(keep, a, 0.0)
    b = np.where(keep, b, 0.0)
    return a, b, keep


def window_sum(x, win):
    # Integral image box sum over win x win windows at every position.
    s = np.pad(x, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    return s[win:, win:] - s[:-win, win:] - s[win:, :-win] + s[:-win, :-win]


def ssim_map(a, b, win=8):
    n = win * win
    mu_a, mu_b = window_sum(a, win) / n, window_sum(b, win) / n
    var_a = window_sum(a * a, win) / n - mu_a ** 2
    var_b = window_sum(b * b, win) / n - mu_b ** 2
    cov = window_sum(a * b, win) / n - mu_a * mu_b
    return ((2 * mu_a * mu_b + C1) * (2 * cov + C2)) / ((mu_a ** 2 + mu_b ** 2 + C1) * (var_a + var_b + C2))


def compare(path_a, path_b, masks=(), heatmap=None, win=8):
    a, b = load_gray(path_a), load_gray(path_b)
    if a.shape != b.shape:
        raise ValueError('size mismatch %s vs %s' % (a.shape, b.shape))
    a, b, keep = apply_masks(a, b, masks)
    m = ssim_map(a, b, win)
    # A window counts only if it lies entirely outside the masks.
    valid = window_sum(keep.astype(np.float64), win) == win * win
    score = float(m[valid].mean()) if valid.any() else float('nan')
    if heatmap:
        err = np.clip(1.0 - m, 0, 1)
        err[~valid] = 0
        full = np.zeros(a.shape)
        full[win // 2:win // 2 + err.shape[0], win // 2:win // 2 + err.shape[1]] = err
        rgb = np.stack([np.clip(full * 4, 0, 1), np.zeros_like(full), np.zeros_like(full)], -1)
        base = np.repeat((b / 255.0 * 0.35)[..., None], 3, -1)
        Image.fromarray((np.clip(base + rgb, 0, 1) * 255).astype(np.uint8)).save(heatmap)
    return score


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--mask', action='append', type=parse_rect, default=[], help='x,y,w,h to exclude')
    p.add_argument('--heatmap')
    p.add_argument('--window', type=int, default=8)
    p.add_argument('--json', action='store_true')
    a = p.parse_args()
    score = compare(a.a, a.b, a.mask, a.heatmap, a.window)
    print(json.dumps({'ssim': score}) if a.json else '%.4f' % score)


if __name__ == '__main__':
    sys.exit(main())
