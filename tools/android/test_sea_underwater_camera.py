#!/usr/bin/env python3
"""Exercise the actual SEA::Realize camera classification with a deterministic wave service.

This verifies the coordinate/startup contract, not GPU rendering or the wave solver.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine-source', type=Path,
                    default=Path(__file__).resolve().parents[2])
parser.add_argument('--cxx', default='c++')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = (args.engine_source / 'src/libs/sea/src/sea.cpp').read_text()
start = source.index('    bUnderSea = false;', source.index('void SEA::Realize('))
end = source.index('    if (bSimpleSea)', start)
actual = source[start:end]
old = 'if (vCamPos.y < WaveXZ(vCamPos.x, vCamPos.z && bStarted))'
new = 'if (bStarted && vCamPos.y < WaveXZ(vCamPos.x, vCamPos.z))'
assert actual.count(new) == 1, 'Re-review the classification block after source changes'
candidate = actual
legacy = actual.replace(new, old)
code = r'''
#include <cassert>
#include <iostream>
struct Camera { float x,y,z; };
struct Probe {
    Camera vCamPos{};
    bool bStarted=false,bUnderSea=false,bUnderSeaStarted=true;
    int samples=0;float sampledX=0,sampledZ=0;
    float WaveXZ(float x,float z) { ++samples;sampledX=x;sampledZ=z;return .001f*x+.01f*z; }
    void original();
    void proposed();
};
''' + 'void Probe::original() {\n' + legacy + '}\n' + 'void Probe::proposed() {\n' + candidate + '}\n' + r'''
int main() {
    // Same actual camera point: original samples z=1, candidate samples z=200.
    {
        Probe old,now;old.vCamPos=now.vCamPos={0,1,200};old.bStarted=now.bStarted=true;
        old.original();now.proposed();
        assert(!old.bUnderSea && now.bUnderSea);
        assert(old.sampledZ==1 && now.sampledZ==200 && now.samples==1);
        assert(!old.bUnderSeaStarted && now.bUnderSeaStarted);
    }
    // Opposite error: false underwater at a positive camera height for negative z.
    {
        Probe old,now;old.vCamPos=now.vCamPos={0,.005f,-200};old.bStarted=now.bStarted=true;
        old.original();now.proposed();
        assert(old.bUnderSea && !now.bUnderSea);
        assert(old.sampledZ==1 && now.sampledZ==-200);
        assert(old.bUnderSeaStarted && !now.bUnderSeaStarted);
    }
    // Exact coordinate contract: keep float x/z, including z=0 and fractional z.
    for(float z:{0.f,.125f,-.125f,200.f,-200.f}) {
        Probe now;now.vCamPos={23.f,3.f,z};now.bStarted=true;now.proposed();
        assert(now.samples==1 && now.sampledX==23.f && now.sampledZ==z);
        assert(!now.bUnderSea && !now.bUnderSeaStarted);
    }
    // First frame never queries not-yet-started wave data; y<0 remains underwater.
    for(float y:{-1.f,0.f,1.f}) {
        Probe old,now;old.vCamPos=now.vCamPos={0,y,1000};
        old.original();now.proposed();
        assert(old.samples==1 && old.sampledZ==0 && now.samples==0);
        assert(now.bUnderSea==(y<0));assert(now.bUnderSeaStarted==(y<0));
    }
    // Started camera below zero remains underwater regardless of sampled wave height.
    {
        Probe now;now.vCamPos={0,-1,-1000};now.bStarted=true;now.proposed();
        assert(now.bUnderSea && now.bUnderSeaStarted && now.samples==1);
    }
    std::cout<<"actual SEA classification: baseline wrong-coordinate reproduced; production x/z/start guard PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-undersea-camera-') as directory:
    cpp=Path(directory)/'check.cpp';binary=Path(directory)/'check'
    cpp.write_text(code)
    subprocess.run([args.cxx,'-std=c++20','-Wall','-Wextra','-Werror',
                    *(['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else []),
                    str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
