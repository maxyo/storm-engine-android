#!/usr/bin/env python3
"""Exercise production AO uniform binding at a fixed camera while the room's volume changes.

Uses a recording bgfx sink, not a GPU: checks which transform is actually submitted.
The existing renderer_bgfx/ao_volume_check covers the shader and rendered result.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/libs/renderer_bgfx/src/effects/lighting.cpp').read_text()
start = source.index('void Lighting::bind_ao_volume(')
end = source.index('\nvoid Lighting::bind_visibility(', start)
method = source[start:end]
header = (root / 'src/libs/renderer_bgfx/src/effects/lighting.hpp').read_text()
# Copy the actual cache storage too: removing a required member must fail this check.
fields = '\n'.join(line for line in header.splitlines() if 'std::array<float, 16> ao_' in line)
fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using Matrix = std::array<float, 16>;
constexpr int BGFX_SAMPLER_POINT = 0;
namespace bgfx {
struct Handle { int idx = -1; };
struct UniformType { enum { Sampler, Mat4, Vec4 }; };
struct TextureFormat { enum { RGBA8 }; };
Matrix recorded{};
int next_id = 1, matrix_id = -1;
bool isValid(Handle h) { return h.idx >= 0; }
Handle createUniform(const char *, int type) { Handle h{next_id++}; if(type == UniformType::Mat4) matrix_id=h.idx; return h; }
const void *copy(const void *p, int) { return p; }
Handle createTexture3D(int,int,int,bool,int,int,const void *) { return {next_id++}; }
void setTexture(int,Handle,Handle) {}
void setUniform(Handle h,const void *p) { if(h.idx == matrix_id) std::memcpy(recorded.data(),p,64); }
}
namespace bx {
int inversions = 0;
// The test deliberately fixes the camera at identity: only the room transform changes.
void mtxInverse(float *out,const float *in) { ++inversions; for(int i=0;i<16;++i) assert(in[i] == (i%5==0?1.f:0.f)); std::memcpy(out,in,64); }
void mtxMul(float *out,const float *a,const float *b) { for(int i=0;i<4;++i) for(int j=0;j<4;++j) {out[4*i+j]=0; for(int k=0;k<4;++k) out[4*i+j]+=a[4*i+k]*b[4*k+j];} }
}
namespace fx {
constexpr int kAoVolumeStage=1;
constexpr uint8_t kAoVolumeLevels=3;
constexpr float kAoOpenSurface=.78f;
struct AoVolumeLevel { float strength,contrast; };
constexpr AoVolumeLevel kAoVolumeLevel[4]={{0,1},{1,1},{1,1},{1,1}};
}
struct Bound { bgfx::Handle texture{1}; Matrix world_to_uvw{}; float cell=.2f; };
struct FfDraw { bool ao=true; Matrix world_view{}; };
class Lighting {
public:
 void bind_ao_volume(const FfDraw &);
 Bound *ao_volume_=nullptr;
 bgfx::Handle s_ao_volume_,u_ao_matrix_,u_ao_params_,ao_open_;
 struct { uint8_t ao_volume=2; } settings_;
FIELDS
};
METHOD
int main() {
 Lighting lighting; Bound room; FfDraw draw;
 for(int i=0;i<4;++i) draw.world_view[i*5]=1;
 room.world_to_uvw=draw.world_view; room.world_to_uvw[0]=.1f; room.world_to_uvw[12]=.3f;
 lighting.ao_volume_=&room; lighting.bind_ao_volume(draw);
 assert(bgfx::recorded==room.world_to_uvw && bx::inversions==1);
 lighting.bind_ao_volume(draw); assert(bx::inversions==1);
 // Same Bound address, same camera, newly baked room with another origin and scale.
 room.world_to_uvw[0]=.25f; room.world_to_uvw[12]=-.75f;
 lighting.bind_ao_volume(draw); assert(bgfx::recorded==room.world_to_uvw && bx::inversions==2);
 lighting.ao_volume_=nullptr; lighting.bind_ao_volume(draw);
 room.world_to_uvw[12]=.8f; lighting.ao_volume_=&room; lighting.bind_ao_volume(draw);
 assert(bgfx::recorded==room.world_to_uvw && bx::inversions==3);
 std::puts("PASS: ready AO volume changes update submitted transform at a stationary camera; stable room reuses it");
}
'''
with tempfile.TemporaryDirectory(prefix='storm-ao-transform-') as folder:
    file = Path(folder) / 'check.cpp'
    file.write_text(fixture.replace('FIELDS', fields).replace('METHOD', method))
    binary = Path(folder) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
