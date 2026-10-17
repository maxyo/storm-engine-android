from pathlib import Path
import subprocess,tempfile,sys
ROOT = Path(__file__).resolve().parents[2]
p = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT / 'src/libs/renderer/src/s_device.cpp'
s=p.read_text();a=s.index('void DX9RENDER::DrawRects(');b=s.index('\nvoid DX9RENDER::DrawSprites(',a)
code=r'''
#include <cassert>
#include <cstdint>
#include <cmath>
#include <array>
#include <iostream>
constexpr int D3DTS_VIEW=0,D3DTS_WORLD=1,D3DLOCK_DISCARD=1,D3D_OK=0,RS_RECT_VERTEX_FORMAT=0,D3DPT_TRIANGLELIST=0;
#define CHECKD3DERR(x) (x)
struct CVECTOR {float x=0,y=0,z=0;CVECTOR(float a=0,float b=0,float c=0):x(a),y(b),z(c){} CVECTOR operator+(CVECTOR v)const{return {x+v.x,y+v.y,z+v.z};}};
struct CMatrix {int marker=1;CVECTOR operator*(CVECTOR v)const{return v;}};
struct RECT_VERTEX {CVECTOR pos;uint32_t color;float u,v;};
struct RS_RECT {CVECTOR vPos;float fSize=1,fAngle=0;uint32_t dwColor=0xffffffff,dwSubTexture=0;};
struct Device {std::array<CMatrix,2> matrices;Device(){matrices[0].marker=42;matrices[1].marker=73;}
 void GetTransform(int i,CMatrix& m){m=matrices[i];} void SetTransform(int i,const CMatrix& m){matrices[i]=m;}};
struct Buffer {std::array<RECT_VERTEX,12> vertices;bool locked=false;int attempts=0,unlocks=0,failAt=0,nullAt=0;
 int Lock(int,size_t bytes,void** p,int){assert(!locked&&bytes<=sizeof(vertices));++attempts;if(attempts==failAt)return -1;locked=true;*p=attempts==nullAt?nullptr:vertices.data();return 0;}
 void Unlock(){assert(locked);locked=false;++unlocks;}};
struct DX9RENDER {Device device;Device* d3d9=&device;Buffer buffer;Buffer* rectsVBuffer=&buffer;
 uint32_t rectsVBuffer_SizeInRects=2;CVECTOR vWordRelationPos;int draws=0,triangles=0;
 float GetHeightDeformator(){return 1;}int SetFVF(int){return 0;}bool TechniqueExecuteStart(const char*){return true;}bool TechniqueExecuteNext(){return false;}
 int SetStreamSource(int,Buffer*,size_t){return 0;}
 int DrawPrimitive(int,int,int n){assert(!buffer.locked&&device.matrices[0].marker==1&&device.matrices[1].marker==1);++draws;triangles+=n;return 0;}
 void DrawRects(RS_RECT*,uint32_t,const char*,uint32_t,uint32_t,float,float);
};
''' + s[a:b] + r'''
int main(){
 std::array<RS_RECT,5> rects;
 for(int failure:{1,2,3})for(bool nullResult:{false,true}){
  DX9RENDER r;if(nullResult)r.buffer.nullAt=failure;else r.buffer.failAt=failure;
  r.DrawRects(rects.data(),rects.size(),"rain_drops",8,1,1,1);
  assert(r.device.matrices[0].marker==42&&r.device.matrices[1].marker==73);
  assert(!r.buffer.locked&&r.draws==failure-1);
  assert(r.buffer.unlocks==failure-1+(nullResult?1:0));
 }
 {DX9RENDER r;r.DrawRects(rects.data(),rects.size(),"rain_drops",8,1,1,1);
  assert(r.draws==3&&r.triangles==10&&r.buffer.unlocks==3&&!r.buffer.locked);
  assert(r.device.matrices[0].marker==42&&r.device.matrices[1].marker==73);}
 {DX9RENDER r;r.DrawRects(nullptr,5,nullptr,1,1,1,1);r.DrawRects(rects.data(),0,nullptr,1,1,1,1);
  assert(r.buffer.attempts==0&&r.device.matrices[0].marker==42&&r.device.matrices[1].marker==73);}
 std::cout<<"actual DrawRects: first/later Lock errors, successful null data, normal batches and no-op restore PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='drawrects-restore-') as d:
 c=Path(d)/'test.cpp';c.write_text(code);exe=Path(d)/'test'
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
