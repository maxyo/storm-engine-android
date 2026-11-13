#!/usr/bin/env python3
"""Builds pbrify-normal-int8.onnx and pbrify-height-int8.onnx (this folder) from PBRify's models (README.md).

1. The ONNX exports of PBRify_Remix's 1x-PBRify_NormalV3 and 1x-PBRify_Height (Kim2091/PBRify_Remix, CC0-1.0;
   exported by fernandotonon/QtMeshEditor-models at revision b35b7f4c, CC0-1.0), checked by sha256. Input and output
   1x3xHxW, 0..1, free size.
2. Static int8 quantisation, QDQ, per-channel weights (onnxruntime.quantization), calibrated on deterministic
   procedural colour textures (noise, bricks, planks, cobbles) at 256 px with the game's 48 px wrap padding: no game
   data goes into the models. What no node uses is dropped.

Needs: pip install onnx==1.23.2 onnxruntime==1.31.1 numpy==2.5.4. Deterministic for the same versions.
"""
import argparse
import hashlib
import urllib.request
from pathlib import Path

import numpy as np
import onnx
from onnxruntime.quantization import CalibrationDataReader, QuantFormat, QuantType, quantize_static

REVISION = 'b35b7f4c924936803bf79e868302d7f8f643d302'
SOURCE = f'https://huggingface.co/fernandotonon/QtMeshEditor-models/resolve/{REVISION}/'
MODELS = {
    'pbrify-normal-int8.onnx': ('1x-PBRify_NormalV3.onnx', '19d78f555f994219e2692876ab0fb6c3e4382503952dbe72d74db89d24870ba4'),
    'pbrify-height-int8.onnx': ('1x-PBRify_Height.onnx', '2b14afd583148cb5b0a554bf86a009b6a898aadc00d213aa3ec2544e17c835f5'),
}
PAD = 48  # fx::kReliefModelPad


def original(name, sha256):
    data = urllib.request.urlopen(SOURCE + name).read()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise SystemExit(f'{name} does not match its pin')
    return data


def prune(model):
    graph = model.graph
    needed = {value.name for value in graph.output}
    kept = []
    for node in reversed(graph.node):
        if any(output in needed for output in node.output):
            kept.append(node)
            needed.update(name for name in node.input if name)
    del graph.node[:]
    graph.node.extend(reversed(kept))
    initializers = [i for i in graph.initializer if i.name in needed]
    del graph.initializer[:]
    graph.initializer.extend(initializers)
    return model


def blur(x, r):
    kernel = np.ones(2 * r + 1) / (2 * r + 1)
    for axis in (0, 1):
        x = np.apply_along_axis(lambda v: np.convolve(np.pad(v, r, mode='wrap'), kernel, 'valid'), axis, x)
    return x


def noise(rng, n=256):
    x = np.zeros((n, n))
    for octave in range(6):
        x += blur(rng.random((n, n)), max(1, 32 >> octave)) * (0.6 ** octave)
    x -= x.min()
    return x / max(x.max(), 1e-6)


def bricks(rng, n=256):
    h, w, m = int(rng.integers(12, 40)), int(rng.integers(24, 80)), int(rng.integers(1, 4))
    y, x = np.mgrid[0:n, 0:n]
    mortar = ((y % h) < m) | (((x + (y // h % 2) * (w // 2)) % w) < m)
    return np.where(mortar, 0.15 + 0.1 * rng.random((n, n)), 0.45 + 0.3 * noise(rng))


def planks(rng, n=256):
    h = int(rng.integers(16, 48))
    y, x = np.mgrid[0:n, 0:n]
    grain = 0.5 + 0.25 * np.sin(x / rng.uniform(3, 9) + 3 * noise(rng))
    return np.where((y % h) < 2, 0.1, grain * (0.6 + 0.4 * noise(rng)))


def cobbles(rng, n=256):
    points = rng.random((60, 2)) * n
    y, x = np.mgrid[0:n, 0:n]
    first, second = np.full((n, n), 1e9), np.full((n, n), 1e9)
    for p in points:
        dy, dx = np.minimum(abs(y - p[0]), n - abs(y - p[0])), np.minimum(abs(x - p[1]), n - abs(x - p[1]))
        distance = np.sqrt(dx * dx + dy * dy)
        second = np.minimum(second, np.maximum(first, distance))
        first = np.minimum(first, distance)
    return 0.2 + 0.6 * np.clip((second - first) / 6, 0, 1) * (0.7 + 0.3 * noise(rng))


class Procedural(CalibrationDataReader):
    """Grey patterns tinted with a stone, wood or plaster colour and a little colour noise, wrap-padded as the game pads."""

    def __init__(self):
        rng = np.random.default_rng(7)
        kinds = [noise, bricks, planks, cobbles]
        items = []
        for i in range(32):
            grey = kinds[i % 4](rng)
            tint = rng.uniform(0.5, 1.0, 3)
            rgb = np.clip(grey[..., None] * tint[None, None] + 0.05 * (rng.random((256, 256, 3)) - 0.5), 0, 1)
            rgb = np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode='wrap')
            items.append({'input': rgb.transpose(2, 0, 1)[None].astype(np.float32)})
        self.items = iter(items)

    def get_next(self):
        return next(self.items, None)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=Path(__file__).parent)
    a = p.parse_args()
    for target, (name, sha256) in MODELS.items():
        staged = a.output / (target + '.float.onnx')
        staged.write_bytes(original(name, sha256))
        out = a.output / target
        quantize_static(str(staged), str(out), Procedural(), quant_format=QuantFormat.QDQ,
                        activation_type=QuantType.QUInt8, weight_type=QuantType.QInt8, per_channel=True)
        staged.unlink()
        onnx.save(prune(onnx.load(out)), out)
        print(out, hashlib.sha256(out.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
