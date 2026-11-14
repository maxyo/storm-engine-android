#!/usr/bin/env python3
"""Compile production foam lookup/dispatch and both wave samples against a destroy/recreate registry."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-ref', help='read production source at a baseline git ref')
a = p.parse_args()
def read(path):
    return subprocess.check_output(['git', '-C', str(root), 'show', f'{a.source_ref}:{path}'], text=True) if a.source_ref else (root/path).read_text()
source = read('src/libs/island/src/foam.cpp')
header = read('src/libs/island/src/foam.h')
realize = source[source.index('void CoastFoam::Realize('):]
lookup = realize[realize.index('    pFrustumPlanes = rs->GetPlanes();') + len('    pFrustumPlanes = rs->GetPlanes();'):realize.index('    auto fDeltaTime')]
dispatch = realize[realize.index('    for (int32_t i = 0; i < aFoams.size(); i++)'):realize.index('    FlushStrands();')]
methods = []
for kind in (1, 2):
    signature = re.search(r'void CoastFoam::ExecuteFoamType' + str(kind) + r'\([^\n]+\)', source).group()
    body = source[source.index(signature):]
    sample = next(line.strip() for line in body.splitlines() if 'vPos.y = fFoamDeltaY +' in line)
    methods.append(signature + '\n{ glm::vec3 vPos{}; ' + sample + ' pF->sampled = vPos.y; }\n')
declarations = '\n'.join(m[:m.index('\n')].replace('CoastFoam::', '') + ';' for m in methods)
cached = 'SEA_BASE *pSea = nullptr;' if re.search(r'SEA_BASE\s*\*\s*pSea\s*;', header) else ''
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
namespace glm { struct vec3 { float x=0, y=0, z=0; }; }
struct SEA_BASE { float height; explicit SEA_BASE(float h):height(h){} float WaveXZ(float, float) { return height; } };
struct Core {
    SEA_BASE *active = nullptr;
    int GetEntityId(const char *name) { assert(std::string(name)=="sea"); return active ? 1 : 0; }
    SEA_BASE *GetEntityPointer(int id) { return id ? active : nullptr; }
    SEA_BASE *GetEntityPointerSafe(int id) { return GetEntityPointer(id); }
} core;
constexpr int FOAM_TYPE_1=1;
struct Foam { int Type; float sampled=-1; };
struct CoastFoam {
    CACHED
    float fFoamDeltaY = 0.25f;
    std::vector<Foam*> aFoams;
    DECLARATIONS
    void Realize(uint32_t Delta_Time) {
        LOOKUP
        float fDeltaTime = float(Delta_Time)*0.001f;
        DISPATCH
    }
};
METHODS
int main() {
    CoastFoam foam;
    Foam a{1}, b{2}; foam.aFoams={&a,&b};
    foam.Realize(16); assert(a.sampled==-1 && b.sampled==-1);
    auto sea = std::make_unique<SEA_BASE>(3.f); core.active=sea.get();
    foam.Realize(16); assert(a.sampled==3.25f && b.sampled==3.25f);
    sea.reset(); core.active=nullptr;
    foam.Realize(16); assert(a.sampled==3.25f && b.sampled==3.25f);
    sea=std::make_unique<SEA_BASE>(7.f); core.active=sea.get();
    foam.Realize(16); assert(a.sampled==7.25f && b.sampled==7.25f);
    puts("PASS production CoastFoam lookup, dispatch and both wave samples: absent/destroyed/recreated sea");
}
'''
for key, value in [('CACHED',cached),('DECLARATIONS',declarations),('LOOKUP',lookup),('DISPATCH',dispatch),('METHODS','\n'.join(methods))]: code=code.replace(key,value)
with tempfile.TemporaryDirectory(prefix='coast-foam-entity-') as d:
    out=Path(d); (out/'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(out/'check.cpp'),'-o',str(out/'check')],check=True)
    subprocess.run([str(out/'check')],check=True)
