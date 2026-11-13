#!/usr/bin/env python3
"""Builds pbrify-upscale-int8.onnx (this folder) from PBRify's 4x SPAN upscaler (README.md, fx/upscale.hpp).

1. Kim2091/PBRify_Remix release v1.7.2 (CC0-1.0; the build downloads and checks it, cmake/StormDerivedFiles.cmake, and
   passes its folder as --sources), its Extras/Old_Models/4x-PBRify_UpscalerSPANV4.pth (checked by sha256 here).
2. Exported to ONNX with spandrel (opset 17, input 1x3xHxW, output 1x3x4Hx4W, float 0..1, free size).
3. The float graph converted for the GPU by pnnx (ncnn's .param / .bin, fp16 weights), as make_ncnn_models.py.
4. Static int8 quantisation, QDQ, per-channel weights (onnxruntime.quantization), calibrated on make_models.py's
   procedural colour textures at 256 px with 16 px of wrap (even) or mirror (odd) padding, as the game pads its tiles:
   no game data goes into the model. What no node uses is dropped.

Needs exactly upscale-requirements.txt (the build makes that venv). Run by the build (storm_derived_files in ../CMakeLists.txt) as: python -I make_upscale_model.py --sources <the release's
folder> --output <dir>; it writes pbrify-upscale-int8.onnx, pbrify-upscale.ncnn.param and pbrify-upscale.ncnn.bin into --output.
"""
import argparse
import hashlib
import importlib.util
import sys
import types
from pathlib import Path

MEMBER = 'Extras/Old_Models/4x-PBRify_UpscalerSPANV4.pth'
MEMBER_SHA256 = 'd1f1fa1af7a5ab23fcf143e75aac4951e6c52d56bf77d9ccd4d004a4831d0461'
PAD = 16


def stub_torchvision():
    tv = types.ModuleType('torchvision')
    transforms = types.ModuleType('torchvision.transforms')
    functional = types.ModuleType('torchvision.transforms.functional')
    def attribute(name):
        if name.startswith('__'):
            raise AttributeError(name)
        return type(name, (), {})

    for module in (tv, transforms, functional):
        module.__getattr__ = attribute
    tv.transforms, transforms.functional = transforms, functional
    sys.modules.update({'torchvision': tv, 'torchvision.transforms': transforms, 'torchvision.transforms.functional': functional})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sources', type=Path, nargs='+', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    # The release's folder, with or without its top folder (PBRify_Remix-1.7.2).
    found = [path for root in a.sources for path in (root / MEMBER, *root.glob('*/' + MEMBER)) if path.is_file()]
    if not found:
        raise SystemExit(f'{MEMBER} is not in {a.sources}')
    pth = found[0].read_bytes()
    if hashlib.sha256(pth).hexdigest() != MEMBER_SHA256:
        raise SystemExit(f'{MEMBER} does not match its pin')
    a.output.mkdir(parents=True, exist_ok=True)

    stub_torchvision()
    import numpy as np
    import onnx
    import spandrel
    import torch
    from onnxruntime.quantization import CalibrationDataReader, QuantFormat, QuantType, quantize_static

    spec = importlib.util.spec_from_file_location('make_models', Path(__file__).parent / 'make_models.py')
    procedural = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(procedural)

    model = spandrel.ModelLoader().load_from_file(str(found[0])).model.eval()
    staged = a.output / 'pbrify-upscale-int8.onnx.float.onnx'
    torch.onnx.export(model, torch.rand(1, 3, 64, 64, generator=torch.Generator().manual_seed(0)), str(staged),
                      input_names=['input'], output_names=['output'], opset_version=17,
                      dynamic_axes={'input': {2: 'h', 3: 'w'}, 'output': {2: 'H', 3: 'W'}}, dynamo=False)

    class Procedural(CalibrationDataReader):
        def __init__(self):
            rng = np.random.default_rng(7)
            kinds = [procedural.noise, procedural.bricks, procedural.planks, procedural.cobbles]
            items = []
            for i in range(32):
                grey = kinds[i % 4](rng)
                tint = rng.uniform(0.5, 1.0, 3)
                rgb = np.clip(grey[..., None] * tint[None, None] + 0.05 * (rng.random((256, 256, 3)) - 0.5), 0, 1)
                rgb = np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode='wrap' if i % 2 == 0 else 'reflect')
                items.append({'input': rgb.transpose(2, 0, 1)[None].astype(np.float32)})
            self.items = iter(items)

        def get_next(self):
            return next(self.items, None)

    out = a.output / 'pbrify-upscale-int8.onnx'
    quantize_static(str(staged), str(out), Procedural(), quant_format=QuantFormat.QDQ, activation_type=QuantType.QUInt8,
                    weight_type=QuantType.QInt8, per_channel=True)
    # The same float graph for the GPU: ncnn's .param / .bin (fp16 weights) by pnnx's standalone binary, as
    # make_ncnn_models.py converts the relief's networks. The shape only traces the graph.
    import importlib.util as util
    import shutil
    import subprocess
    import tempfile
    tool = Path(util.find_spec('pnnx').submodule_search_locations[0]) / 'pnnx'
    with tempfile.TemporaryDirectory() as work:
        graph = Path(work) / 'model.onnx'
        shutil.copyfile(staged, graph)
        done = subprocess.run([str(tool), str(graph), 'inputshape=[1,3,176,176]'], cwd=work, capture_output=True, text=True)
        if done.returncode:
            raise SystemExit(f'pnnx failed:\n{done.stdout}{done.stderr}')
        for suffix in ('param', 'bin'):
            target = a.output / f'pbrify-upscale.ncnn.{suffix}'
            shutil.copyfile(Path(work) / f'model.ncnn.{suffix}', target)
            print(target, hashlib.sha256(target.read_bytes()).hexdigest())
    staged.unlink()
    onnx.save(procedural.prune(onnx.load(out)), out)
    print(out, hashlib.sha256(out.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
