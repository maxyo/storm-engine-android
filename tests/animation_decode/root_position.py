#!/usr/bin/env python3
"""Root bone position between two frames never reads past the last frame.

Compiles the production Bone::BlendPosition with a position track of exactly numFrames entries under
ASan/UBSan: the last frame (and any later one) holds, interior frames interpolate as before. The old
expression pos[f] + k * (pos[f + 1] - pos[f]) read pos[numFrames] at the last frame (found by ASan on a
sea save: sailors' animations, AnimationImp::BuildAnimationMatrices).
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--glm-source', required=True, type=Path)
args = parser.parse_args()
source = (root / 'src/libs/animation/src/bone.cpp').read_text()
start = source.index('glm::vec3 Bone::BlendPosition(')
end = source.index('\n}\n', start) + 3
code = r'''
#include "storm_glm.h"
#include <cassert>
#include <cstdint>
#include <iostream>
struct Bone {
    glm::vec3 *pos = nullptr;
    int32_t numFrames = 0;
    glm::vec3 BlendPosition(int32_t frame, float kBlend) const;
};
''' + source[start:end] + r'''
int main()
{
    Bone bone;
    bone.numFrames = 3;
    bone.pos = new glm::vec3[3]{glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, 6.0f), glm::vec3(10.0f)};
    assert(bone.BlendPosition(0, 0.5f) == glm::vec3(1.0f, 2.0f, 3.0f));
    assert(bone.BlendPosition(1, 0.0f) == glm::vec3(2.0f, 4.0f, 6.0f));
    assert(bone.BlendPosition(2, 0.0f) == glm::vec3(10.0f));
    assert(bone.BlendPosition(2, 0.75f) == glm::vec3(10.0f)); // the last frame holds
    bone.numFrames = 1;
    assert(bone.BlendPosition(0, 0.5f) == glm::vec3(0.0f));
    delete[] bone.pos;
    std::cout << "actual Bone::BlendPosition: interior frames interpolate, the last frame holds, no read past the track PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-root-position-') as temporary:
    output = Path(temporary)
    (output / 'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I' + str(root / 'src/libs/math/include'),
                    '-isystem', str(args.glm_source), str(output / 'check.cpp'), '-o', str(output / 'check')],
                   check=True)
    subprocess.run([str(output / 'check')], check=True)
