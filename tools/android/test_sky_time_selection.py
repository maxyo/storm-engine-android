#!/usr/bin/env python3
"""Run actual SKY texture-selection methods against host weather/texture services.

No GPU, full engine build or Android device is needed. Method bodies and the SKY
class declaration are read from production sources; only external services and
unrelated virtual methods are stubbed. Checks texture paths, references, failed
loads and the selected blend phase, not visual fidelity.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def method(source: str, name: str) -> str:
    match = re.search(r"^(?:[\w:]+\s+)?SKY::" + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise RuntimeError(f"Cannot find production SKY::{name}")
    # Production method closing braces start in column zero, unlike nested scopes.
    end = source.index("\n}", match.start()) + 2
    return source[match.start():end]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = (args.engine_source / "src/libs/weather/src/sky.cpp").read_text()
    header = (args.engine_source / "src/libs/weather/src/sky.h").read_text()
    header = re.sub(r'^#include "[^"\n]+"\n|^#pragma once\n', '', header, flags=re.M)
    methods = '\n'.join(method(source, name) for name in (
        'SKY', '~SKY', 'Release', 'LoadTextures', 'FillSkyDirArray',
        'TimeToFactor', 'SelectSkyTextures', 'UpdateTimeFactor', 'GenerateSky', 'RestoreRender'))
    harness = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
struct ATTRIBUTES {
    std::string value;
    std::vector<std::pair<std::string,std::string>> entries;
    int32_t GetAttributesNum() const { return int32_t(entries.size()); }
    const char* GetAttributeName(int32_t i) const { return entries.at(i).first.c_str(); }
    const char* GetAttribute(uint32_t i) const { return entries.at(i).second.c_str(); }
    const char* GetThisAttr() const { return value.c_str(); }
};
std::string to_string(const char* value) { return value; }
struct ENTITY_STATE_GEN {};
struct ENTITY_STATE {};
struct MESSAGE {};
struct CVECTOR {};
enum class Stage { realize, restore_render };
struct Entity {
    virtual ~Entity()=default;
    virtual bool Init() { return true; }
    virtual uint32_t AttributeChanged(ATTRIBUTES*) { return 0; }
    virtual uint64_t ProcessMessage(MESSAGE&) { return 0; }
    virtual void ProcessStage(Stage,uint32_t) {}
};
struct VDX9RENDER;
namespace storm::render {
    struct ColoredMesh {};
    struct TexturedMesh { void update(int,int) {} };
    std::unique_ptr<TexturedMesh> makeLegacyTexturedMesh(VDX9RENDER&) { return std::make_unique<TexturedMesh>(); }
}
namespace storm::weather {
    struct SkyMesh { int vertices=0,indices=0; };
    SkyMesh makeSkyMesh(float size) { assert(std::isfinite(size));return {}; }
}
using entid_t=int;
constexpr int whf_time_counter=0;
struct WEATHER_BASE:Entity {
    float hours=0;
    float GetFloat(int key) { assert(key==whf_time_counter); return hours; }
};
struct Core {
    WEATHER_BASE weather;
    bool present=true;
    entid_t GetEntityId(const char* name) { assert(std::string(name)=="weather"); return present?1:0; }
    Entity* GetEntityPointer(entid_t id) { assert(id==1); return &weather; }
} core;
struct VDX9RENDER {
    struct Texture { std::string path; int refs; };
    std::vector<Texture> textures;
    int creates=0, increments=0, releases=0;
    int failCreate=-1, throwCreate=-1, failIncrement=-1;
    int32_t TextureCreate(const char* path) {
        ++creates;
        if(creates==throwCreate) throw std::runtime_error("injected load exception");
        if(creates==failCreate) return -1;
        for(size_t i=0;i<textures.size();++i)
            if(textures[i].refs && textures[i].path==path) { ++textures[i].refs;return int32_t(i); }
        textures.push_back({path,1});return int32_t(textures.size()-1);
    }
    bool TextureIncReference(int32_t id) {
        assert(id>=0 && textures.at(id).refs>0);
        if(++increments==failIncrement) return false;
        ++textures.at(id).refs;return true;
    }
    bool TextureRelease(int32_t id) {
        assert(id>=0 && textures.at(id).refs>0);
        --textures.at(id).refs;++releases;return true;
    }
};
#define private public
''' + header + r'''
#undef private
// Virtual slots and rendering are outside this test's boundary.
bool SKY::Init() { return true; }
uint32_t SKY::AttributeChanged(ATTRIBUTES*) { return 0; }
uint64_t SKY::ProcessMessage(MESSAGE&) { return 0; }
void SKY::Realize(uint32_t) {}
void SKY::UpdateFogSphere(bool) {}
''' + methods + r'''
constexpr const char* faces[]={"sky_fr.tga","sky_lf.tga","sky_bk.tga","sky_rt.tga","sky_up.tga"};
void ownership(const SKY& sky,const VDX9RENDER& renderer) {
    std::vector<int> expected(renderer.textures.size());
    for(int i=0;i<5;++i) {
        if(sky.TexturesID[i]>=0)++expected.at(sky.TexturesID[i]);
        if(sky.TexturesNextID[i]>=0)++expected.at(sky.TexturesNextID[i]);
    }
    for(size_t i=0;i<expected.size();++i)assert(expected[i]==renderer.textures[i].refs);
}
void pair(const SKY& sky,const VDX9RENDER& renderer,const std::string& a,const std::string& b,float factor) {
    assert(sky.loadedSkyDirs_[0]==a && sky.loadedSkyDirs_[1]==b);
    assert(std::abs(sky.fTimeFactor-factor)<0.0001f);
    for(int i=0;i<5;++i) {
        assert(sky.TexturesID[i]>=0 && renderer.textures.at(sky.TexturesID[i]).path==a+faces[i]);
        if(b.empty())assert(sky.TexturesNextID[i]==-1);
        else assert(sky.TexturesNextID[i]>=0 && renderer.textures.at(sky.TexturesNextID[i]).path==b+faces[i]);
    }
    ownership(sky,renderer);
}
void fill(SKY& sky,int count,float hours) {
    ATTRIBUTES attributes;attributes.value=std::to_string(hours);
    for(int i=0;i<count;++i)attributes.entries.push_back({"d"+std::to_string(i),std::to_string(i)+"/"});
    sky.FillSkyDirArray(&attributes);
}
void update(SKY& sky,float hours) { core.weather.hours=hours;sky.UpdateTimeFactor(); }
struct Fixture {
    VDX9RENDER renderer;
    SKY sky;
    Fixture(int count,float hours) {
        core.present=true;sky.pRS=&renderer;sky.fSkySize=2000.f;fill(sky,count,hours);sky.LoadTextures();
        sky.fAngleY=0.375f;
    }
    ~Fixture() { sky.Release();ownership(sky,renderer); }
};
int main() {
    {
        Fixture f(24,12.25f);auto& s=f.sky;auto& r=f.renderer;
        pair(s,r,"12/","13/",12.25f);assert(r.creates==10);
        update(s,12.8f);update(s,12.1f);update(s,36.6f);
        pair(s,r,"12/","13/",12.6f);assert(r.creates==10 && r.increments==0 && r.releases==0);
        update(s,13.25f);pair(s,r,"13/","14/",13.25f);assert(r.creates==15);
        update(s,18.25f);pair(s,r,"18/","19/",18.25f);assert(r.creates==25);
        update(s,9.25f);pair(s,r,"9/","10/",9.25f);assert(r.creates==35);
        update(s,10.25f);pair(s,r,"10/","11/",10.25f);assert(r.creates==40);
        update(s,23.75f);pair(s,r,"23/","0/",23.75f);assert(r.creates==50);
        update(s,0.25f);pair(s,r,"0/","1/",0.25f);assert(r.creates==55);
        update(s,23.75f);pair(s,r,"23/","0/",23.75f);assert(r.creates==60);
        update(s,-0.25f);pair(s,r,"23/","0/",23.75f);assert(r.creates==60);
        update(s,48.25f);pair(s,r,"0/","1/",0.25f);assert(r.creates==65);
        assert(s.fAngleY==0.375f);
        core.present=false;update(s,8);pair(s,r,"0/","1/",0.25f);core.present=true;
        update(s,std::numeric_limits<float>::quiet_NaN());update(s,std::numeric_limits<float>::infinity());
        pair(s,r,"0/","1/",0.25f);
    }
    {
        Fixture f(24,24);pair(f.sky,f.renderer,"0/","1/",0);
        update(f.sky,24);pair(f.sky,f.renderer,"0/","1/",0);assert(f.renderer.creates==10);
        update(f.sky,std::nextafter(24.f,0.f));
        assert(f.sky.fTimeFactor<24 && f.sky.fTimeFactor>=23);
        pair(f.sky,f.renderer,"23/","0/",std::nextafter(24.f,0.f));
    }
    {
        Fixture f(1,24);pair(f.sky,f.renderer,"0/","",0);assert(f.renderer.creates==5);
        update(f.sky,18);pair(f.sky,f.renderer,"0/","",0.75f);assert(f.renderer.creates==5);
    }
    {
        Fixture f(2,0);pair(f.sky,f.renderer,"0/","1/",0);
        update(f.sky,18);pair(f.sky,f.renderer,"1/","0/",1.5f);
        update(f.sky,3);pair(f.sky,f.renderer,"0/","1/",0.25f);
        assert(f.renderer.creates==10 && f.sky.fAngleY==0.375f);
    }
    // Distinct directory slots can refer to the exact same cached texture IDs.
    {
        Fixture f(2,0);f.sky.aSkyDirArray[1]="0/";update(f.sky,0);
        pair(f.sky,f.renderer,"0/","0/",0);
        for(int i=0;i<5;++i)assert(f.sky.TexturesID[i]==f.sky.TexturesNextID[i]);
        int before=f.renderer.creates;update(f.sky,18);
        pair(f.sky,f.renderer,"0/","0/",1.5f);assert(f.renderer.creates==before);
        f.sky.aSkyDirArray[1]="replacement/";update(f.sky,18);
        pair(f.sky,f.renderer,"replacement/","0/",1.5f);
    }
    // Every returned-failure/throw location in a full two-set replacement rolls back.
    for(bool throwing:{false,true})for(int offset=1;offset<=10;++offset) {
        Fixture f(24,12.25f);auto& s=f.sky;auto& r=f.renderer;
        if(throwing)r.throwCreate=r.creates+offset;else r.failCreate=r.creates+offset;
        bool caught=false;try { update(s,18.25f); }catch(const std::runtime_error&) { caught=true; }
        assert(caught==throwing);pair(s,r,"12/","13/",12.25f);assert(s.fAngleY==0.375f);
        int before=r.creates;
        r.failCreate=r.throwCreate=-1;
        if(!throwing) {
            update(s,18.8f);pair(s,r,"12/","13/",12.25f);assert(r.creates==before);
            update(s,12.5f);pair(s,r,"12/","13/",12.5f); // leaving failed pair permits retry
        }
        update(s,18.75f);pair(s,r,"18/","19/",18.75f);
    }
    // Reused handles survive a later load failure; failed increment does not leak.
    for(bool incrementFailure:{false,true}) {
        Fixture f(24,12.25f);auto& s=f.sky;auto& r=f.renderer;
        if(incrementFailure)r.failIncrement=r.increments+3;else r.failCreate=r.creates+3;
        update(s,13.25f);pair(s,r,"12/","13/",12.25f);
        int before=r.creates;update(s,13.7f);assert(r.creates==before);
        r.failIncrement=r.failCreate=-1;update(s,14.25f);pair(s,r,"14/","15/",14.25f);
    }
    // No initial pair: all temporary refs released; explicit reload retries once.
    {
        VDX9RENDER r;SKY s;s.pRS=&r;fill(s,24,12.25f);r.failCreate=4;s.LoadTextures();
        assert(s.loadedSkyDirs_[0].empty());ownership(s,r);
        int before=r.creates;update(s,12.8f);assert(r.creates==before);
        r.failCreate=-1;s.LoadTextures();pair(s,r,"12/","13/",12.8f);s.Release();ownership(s,r);
    }
    // Attribute setup is lazy, including real isDone generation and restore flow.
    {
        Fixture f(24,12.25f);auto& s=f.sky;auto& r=f.renderer;
        int before=r.creates;fill(s,24,24);assert(r.creates==before);
        pair(s,r,"12/","13/",12.25f);
        s.GenerateSky(true);pair(s,r,"0/","1/",0);assert(s.fAngleY==0.f);
        s.fAngleY=0.375f;before=r.creates;s.RestoreRender();
        pair(s,r,"0/","1/",0);assert(r.creates==before && s.fAngleY==0.375f);
        fill(s,24,12.25f);s.GenerateSky(true);pair(s,r,"12/","13/",12.25f);
        fill(s,1,12);r.failCreate=r.creates+1;
        assert(!s.SelectSkyTextures(s.requestedSkyTimeFactor_));
        pair(s,r,"12/","13/",12.25f);
        r.failCreate=-1;s.GenerateSky(true);pair(s,r,"0/","",0.5f);
        fill(s,24,18.25f);r.failCreate=r.creates+1;s.GenerateSky(true);
        assert(s.loadedSkySetCount_==0);ownership(s,r);
        r.failCreate=-1;s.GenerateSky(true);pair(s,r,"18/","19/",18.25f);
    }
    {
        Fixture f(0,0);assert(f.sky.loadedSkyDirs_[0].empty());ownership(f.sky,f.renderer);
        update(f.sky,12);assert(f.renderer.creates==0);
        assert(!f.sky.SelectSkyTextures(std::numeric_limits<float>::quiet_NaN()));
        fill(f.sky,2,0);f.sky.aSkyDirArray[1].clear();f.sky.LoadTextures();
        assert(f.renderer.creates==10 && f.sky.loadedSkySetCount_==2);
        for(int i=0;i<5;++i)assert(f.renderer.textures.at(f.sky.TexturesNextID[i]).path==faces[i]);
        ownership(f.sky,f.renderer);
        fill(f.sky,1,0);f.sky.aSkyDirArray[0].clear();f.sky.LoadTextures();
        pair(f.sky,f.renderer,"","",0);assert(f.sky.loadedSkySetCount_==1);
        fill(f.sky,2,0);for(auto& dir:f.sky.aSkyDirArray)dir.clear();f.sky.LoadTextures();
        assert(f.sky.loadedSkySetCount_==2);
        for(int i=0;i<5;++i)assert(f.sky.TexturesID[i]==f.sky.TexturesNextID[i]);
        ownership(f.sky,f.renderer);
    }
    std::cout<<"actual SKY methods: time jumps/wrap/initial24/same interval/N1/N2/aliases/rollback/retry/rotation PASS\n";
}
'''
    with tempfile.TemporaryDirectory(prefix='storm-sky-time-') as directory:
        cpp = Path(directory) / 'check.cpp'
        binary = Path(directory) / 'check'
        cpp.write_text(harness)
        subprocess.run([args.cxx, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        *(['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
