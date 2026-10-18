#!/usr/bin/env python3
"""Production effect preference cache: independent settings, default on, restart boundary."""
from pathlib import Path
import tempfile, subprocess, os
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='graphics-effects-') as directory:
    d=Path(directory);(d/'platform').mkdir()
    (d/'platform/android_ui.hpp').write_text('''#pragma once
#include <string>
#include <map>
namespace storm::android {
inline std::map<std::string,int> settings,calls;
inline int graphicsSetting(const char* key,int fallback) {++calls[key];auto i=settings.find(key);return i==settings.end()?fallback:i->second;}
}
''')
    (d/'check.cpp').write_text('''#include "platform/android_graphics_effects.hpp"
#include <cassert>
#include <cstdlib>
int main(int argc,char** argv){
 using namespace storm::android;
 int shadows=std::atoi(argv[1]),foam=std::atoi(argv[2]);
 if(shadows>=0)settings["character_shadows"]=shadows;
 if(foam>=0)settings["sea_foam"]=foam;
 bool expectedShadows=shadows!=0,expectedFoam=foam!=0;
 assert(CharacterShadowsEnabled()==expectedShadows);assert(SeaFoamEnabled()==expectedFoam);
 settings["character_shadows"]=!expectedShadows;settings["sea_foam"]=!expectedFoam;
 for(int i=0;i<100;++i){assert(CharacterShadowsEnabled()==expectedShadows);assert(SeaFoamEnabled()==expectedFoam);}
 assert(calls["character_shadows"]==1&&calls["sea_foam"]==1);
}
''')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(root/'src/libs/util/include'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
    for shadow in [-1,0,1]:
        for foam in [-1,0,1]:subprocess.run([str(d/'check'),str(shadow),str(foam)],check=True)
print('PASS production effects: independent defaults/cuts, one JNI read each, edits apply next process')
