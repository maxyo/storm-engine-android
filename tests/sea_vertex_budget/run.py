#!/usr/bin/env python3
"""SEA's vertex budget: the production tree, frustum, budget and index code (sea.cpp) on a stub SEA.

For the game's sea set-ups (TEHO and Caribbean Legend WhrSea.c) and cameras around the compass, every frame must fit
its vertices into the sea's buffer (NUM_VERTEXS, 16-bit indices) and keep the blocks at the camera; when blocks do not
fit, the farthest go. The original cap (iSize0^2 points per block, the tail of the far-to-near order dropped) is run
as the reference that must fail: it drops the blocks at the camera or lets the grid overflow the buffer."""
import argparse
from pathlib import Path
import os
import re
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
defines = '\n'.join(re.findall(r'^#define NUM_(?:VERTEXS|INDICES) \d+$', production, re.M))
tree = '\n'.join(function(production, signature) for signature in (
    'uint32_t SEA::TreeDim() const', 'void SEA::PlaceTree()', 'int32_t SEA::VisCode(', 'bool SEA::isVisibleBBox(',
    'inline float SEA::CalcLod(', 'void SEA::CalculateLOD(', 'void SEA::AddBlock(', 'void SEA::BuildTree(',
    'void SEA::PrepareIndicesForBlock('))
fit = function(production, 'void SEA::FitBlocksToBuffer()')
# The reference: the cap before the fix, on the same tree.
original = '''void SEA::FitBlocksToBuffer()
{
    std::sort(aBlocks.begin(), aBlocks.end(), [](const SeaBlock &b1, const SeaBlock &b2) { return b1.iLOD > b2.iLOD; });
    int32_t iNumVPoints = 0;
    for (uint32_t i = 0; i < aBlocks.size(); i++)
    {
        iNumVPoints += aBlocks[i].iSize0 * aBlocks[i].iSize0;
        if (iNumVPoints >= NUM_VERTEXS)
        {
            aBlocks.erase(aBlocks.begin() + i, aBlocks.end());
            break;
        }
    }
}
'''
with tempfile.TemporaryDirectory(prefix='storm-sea-budget-') as temporary:
    out = Path(temporary)
    (out / 'defines.inc').write_text(defines + '\n')
    (out / 'tree.inc').write_text(tree + '\n')
    (out / 'production.inc').write_text(fit + '\n')
    (out / 'original.inc').write_text(original + '\n')
    binary = out / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-I' + str(out),
                    *math_include_flags(args.glm_source), str(Path(__file__).with_name('check.cpp')), '-o',
                    str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
