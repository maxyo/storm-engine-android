#!/usr/bin/env python3
"""SEA's block tree across its camera steps: the production placement, LOD and tree code (sea.cpp) on a stub SEA.

A fixed world point within the sea's relief keeps its vertex lattice, LOD and interpolated height while the camera
moves across a step of the tree origin (PlaceTree). The original placement (256-cell steps over a tree of MaxDim
cells, the same tree code otherwise) is built as the reference that jumps."""
import argparse
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # keep the source tree clean
sys.path.insert(0, str(root / 'tests/glm_math'))
from glm_source import math_include_flags
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
args = parser.parse_args()


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


production = (root / 'src/libs/sea/src/sea.cpp').read_text()
tree = '\n'.join(function(production, signature) for signature in (
    'inline float SEA::CalcLod(', 'void SEA::CalculateLOD(', 'void SEA::AddBlock(', 'void SEA::BuildTree('))
placement = '\n'.join(function(production, signature) for signature in ('uint32_t SEA::TreeDim() const', 'void SEA::PlaceTree()'))
# The reference: the original placement on a tree of MaxDim cells.
original = '''uint32_t SEA::TreeDim() const
{
    return dwMaxDim;
}

void SEA::PlaceTree()
{
    float fBlockSize = 256.0f * fGridStep;
    int32_t iNumBlocks = static_cast<int32_t>(dwMaxDim) / (256 * 2);
    vSeaCenterPos = glm::vec3(fBlockSize * (static_cast<int32_t>(vCamPos.x / fBlockSize) - iNumBlocks), fMaxSeaHeight * 0.5f,
                            fBlockSize * (static_cast<int32_t>(vCamPos.z / fBlockSize) - iNumBlocks));
}
'''
with tempfile.TemporaryDirectory(prefix='storm-sea-tree-') as temporary:
    out = Path(temporary)
    (out / 'production.inc').write_text(placement + '\n' + tree)
    (out / 'original.inc').write_text(original + '\n' + tree)
    binary = out / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(out), *math_include_flags(args.glm_source),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
