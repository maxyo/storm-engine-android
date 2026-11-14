#!/usr/bin/env python3
"""Converts PBRify's float models (the same downloads as make_models.py) into ncnn's graphs for the GPU path:
pbrify-{normal,height,roughness}.ncnn.param and .ncnn.bin (README.md). The build runs it (src/libs/relief/CMakeLists.txt):
pnnx, the converter of ncnn's own release (the pnnx package of requirements-ncnn.txt: its standalone binary, no torch),
turns each ONNX graph into ncnn's, fp16 weights. Deterministic for the same pnnx: the build checks the results' digests.
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))  # the build runs us with python -I
from pins import MODELS  # noqa: E402  the float models and their pins

TARGETS = {'pbrify-normal-int8.onnx': 'pbrify-normal', 'pbrify-height-int8.onnx': 'pbrify-height',
           'pbrify-roughness-int8.onnx': 'pbrify-roughness'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sources', type=Path, nargs='+', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    # The package's binary (importing the package would want torch).
    import importlib.util
    tool = Path(importlib.util.find_spec('pnnx').submodule_search_locations[0]) / 'pnnx'
    a.output.mkdir(parents=True, exist_ok=True)
    for int8, (name, sha256) in MODELS.items():
        found = [folder / name for folder in a.sources if (folder / name).is_file()]
        if not found or hashlib.sha256(found[0].read_bytes()).hexdigest() != sha256:
            raise SystemExit(f'{name}: missing or not its pin {sha256}')
        with tempfile.TemporaryDirectory() as work:
            model = Path(work) / 'model.onnx'
            shutil.copyfile(found[0], model)
            # The shape only traces the graph: ncnn's convolutions take any size.
            done = subprocess.run([str(tool), str(model), 'inputshape=[1,3,304,304]'], cwd=work, capture_output=True,
                                  text=True)
            if done.returncode:
                raise SystemExit(f'pnnx failed on {name}:\n{done.stdout}{done.stderr}')
            for suffix in ('param', 'bin'):
                out = a.output / f'{TARGETS[int8]}.ncnn.{suffix}'
                shutil.copyfile(Path(work) / f'model.ncnn.{suffix}', out)
                print(out, hashlib.sha256(out.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
