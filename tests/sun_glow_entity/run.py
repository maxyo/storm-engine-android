#!/usr/bin/env python3
"""Compile production SUNGLOW::GetSunFadeoutFactor and destroy/recreate the registered sky under ASan/UBSan."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-ref',help='read production source at a baseline git ref')
a=p.parse_args()
def read(path):
    return subprocess.check_output(['git','-C',str(root),'show',f'{a.source_ref}:{path}'],text=True) if a.source_ref else (root/path).read_text()
source=read('src/libs/weather/src/sun_glow.cpp')
header=read('src/libs/weather/src/sun_glow.h')
start=source.index('float SUNGLOW::GetSunFadeoutFactor(')
method=source[start:source.index('\n}',start)+2]
cached='SKY *pSky = nullptr;' if re.search(r'SKY\s*\*\s*pSky\s*;',header) else ''
code=r'''
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
namespace glm { struct vec3 { float x=0, y=0, z=0; }; }
struct SKY {
    float alpha; explicit SKY(float a):alpha(a){}
    float CalculateAlphaForSun(const glm::vec3&,float size) { assert(size==2.f); return alpha; }
};
struct Core {
    SKY *active=nullptr;
    int GetEntityId(const char *name) { assert(std::string(name)=="sky"); return active ? 1:0; }
    SKY *GetEntityPointer(int id) { return id ? active:nullptr; }
    SKY *GetEntityPointerSafe(int id) { return GetEntityPointer(id); }
} core;
struct SUNGLOW { CACHED float GetSunFadeoutFactor(const glm::vec3&,float); };
METHOD
int main() {
    SUNGLOW glow;
    assert(glow.GetSunFadeoutFactor({},2.f)==1.f);
    auto sky=std::make_unique<SKY>(0.25f); core.active=sky.get();
    assert(glow.GetSunFadeoutFactor({},2.f)==0.25f);
    sky.reset(); core.active=nullptr;
    assert(glow.GetSunFadeoutFactor({},2.f)==1.f);
    sky=std::make_unique<SKY>(0.75f); core.active=sky.get();
    assert(glow.GetSunFadeoutFactor({},2.f)==0.75f);
    puts("PASS production sun-glow fadeout: absent/destroyed/recreated sky, current alpha and no-sky fallback");
}
'''.replace('CACHED',cached).replace('METHOD',method)
with tempfile.TemporaryDirectory(prefix='sun-glow-entity-') as d:
    out=Path(d);(out/'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(out/'check.cpp'),'-o',str(out/'check')],check=True)
    subprocess.run([str(out/'check')],check=True)
