#!/usr/bin/env python3
"""Builds deepbump256-int8.onnx (this folder) from DeepBump's model (README.md: provenance, licence).

1. The original deepbump256.onnx of HugoTini/DeepBump at commit fad19ba8 (checked by sha256), opset 11 -> 13.
2. Its five 2x upsamplings become Resize with constant scales (the original computes their sizes from shapes, which
   the quantiser fixes to the calibration size; those computations are dropped), and the input and output get a free size: the game runs the model
   once on a whole texture (fx/deepbump.hpp) instead of on 256x256 tiles.
3. Static int8 quantisation, QDQ, per-channel weights (onnxruntime.quantization), calibrated on deterministic
   procedural textures (noise, bricks, planks, cobbles): no game data goes into the model. What no node uses is
   dropped (onnxruntime would warn about every such initializer at each session).

Needs: pip install onnx==1.23.2 onnxruntime==1.31.1 numpy==2.5.4. Deterministic for the same versions.
"""
import argparse
import hashlib
import io
import tarfile
import urllib.request
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper, version_converter
from onnxruntime.quantization import CalibrationDataReader, QuantFormat, QuantType, quantize_static

COMMIT = 'fad19ba87daed12b1d0410a57e74f3d79e82f78d'
ARCHIVE = f'https://codeload.github.com/HugoTini/DeepBump/tar.gz/{COMMIT}'
ARCHIVE_SHA256 = '213f64f9f38811ae67ef4bb16d38d851bb5c8af2efc2e368b445ab809b38262a'
MODEL_SHA256 = '3a2ababc5fa652b19040a0f3d639b71826368e52ad805cf70ffb0e7c96a54d67'


def original():
    data = urllib.request.urlopen(ARCHIVE).read()
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise SystemExit('DeepBump archive does not match its pin')
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        model = archive.extractfile(f'DeepBump-{COMMIT}/deepbump256.onnx').read()
    if hashlib.sha256(model).hexdigest() != MODEL_SHA256:
        raise SystemExit('deepbump256.onnx does not match its pin')
    return onnx.load_from_string(model)


def free_size(model):
    model = version_converter.convert_version(model, 13)
    model.graph.initializer.append(numpy_helper.from_array(np.array([1, 1, 2, 2], np.float32), 'storm_scales_2x'))
    for node in model.graph.node:
        if node.op_type == 'Resize':
            del node.input[1:]
            node.input.extend(['', 'storm_scales_2x'])
    for value in list(model.graph.input) + list(model.graph.output):
        for dim in value.type.tensor_type.shape.dim[2:]:
            dim.ClearField('dim_value')
            dim.dim_param = 'size'
    del model.graph.value_info[:]
    return prune(model)


def prune(model):
    """Drops what no longer feeds the output: the original's size computations of the upsamplings."""
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
    def __init__(self):
        rng = np.random.default_rng(7)
        kinds = [noise, bricks, planks, cobbles]
        self.items = iter([{'input': kinds[i % 4](rng)[None, None].astype(np.float32)} for i in range(32)])

    def get_next(self):
        return next(self.items, None)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=Path(__file__).with_name('deepbump256-int8.onnx'))
    a = p.parse_args()
    staged = a.output.with_suffix('.float.onnx')
    onnx.save(free_size(original()), staged)
    quantize_static(str(staged), str(a.output), Procedural(), quant_format=QuantFormat.QDQ,
                    activation_type=QuantType.QUInt8, weight_type=QuantType.QInt8, per_channel=True)
    staged.unlink()
    onnx.save(prune(onnx.load(a.output)), a.output) # and the float weights the quantiser replaced
    print(a.output, hashlib.sha256(a.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
