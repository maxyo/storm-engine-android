from pathlib import Path
import subprocess,tempfile,sys
ROOT = Path(__file__).resolve().parents[2]
p = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT / 'src/libs/weather/src/rain.cpp'
s=p.read_text();a=s.index('    // draw circles on the water');b=s.index('\n}\n\nvoid RAIN::Realize(',a)
code=r'''
#include <vector>
#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <iostream>
constexpr int NUM_SEA_DROPS=500,D3DLOCK_DISCARD=1;
struct CVECTOR {float x,y,z;CVECTOR(float X=0,float Y=0,float Z=0):x(X),y(Y),z(Z){} CVECTOR operator+(CVECTOR v){return {x+v.x,y+v.y,z+v.z};}};
struct CMatrix {void SetIdentity(){}};
struct SEADROPVERTEX {CVECTOR vPos;uint32_t dwColor;float tu,tv;};
struct seadrop_t {CVECTOR vPos;float fTime,fLifeTime;};
struct Sea {float WaveXZ(float,float){return 2.f;}};
struct Renderer {
 std::array<SEADROPVERTEX,NUM_SEA_DROPS*4> data{};
 bool locked=false;int attempts=0,locks=0,unlocks=0,failAt=0;
 std::vector<int> batches;
 void SetWorld(CMatrix&){} void TextureSet(int,int){}
 void* LockVertexBuffer(int,int){assert(!locked);++attempts;if(attempts==failAt)return nullptr;locked=true;++locks;return data.data();}
 void UnLockVertexBuffer(int){assert(locked);locked=false;++unlocks;}
 void DrawBuffer(int,int stride,int,int,int vertices,int,int triangles,const char*){
  assert(!locked && stride==sizeof(SEADROPVERTEX));assert(vertices>0&&vertices<=2000&&triangles==vertices/2);batches.push_back(vertices/4);
  for(int i=0;i<vertices;i+=4){
   assert(std::abs(data[i].vPos.y-2.015f)<0.0001f);
   assert(data[i].dwColor==0xffffffffu && data[i].tv==0.f && data[i+2].tv==1.f);
   assert(std::abs(data[i+1].vPos.x-data[i].vPos.x-0.3f)<0.0001f);
   assert(std::abs(data[i+1].tu-data[i].tu-0.125f)<0.0001f);
  }
 }
};
void actual(Renderer* rs,Sea* pSea,std::vector<seadrop_t>& aSeaDrops){
 int iSeaDropTex=0,iVBSeaDrops=0,iIBSeaDrops=0;float fDeltaTime=0.1f;
''' + s[a:b] + r'''
}
int main(){
 for(int count:{0,499,500,501,1000}){
  Renderer r;Sea sea;std::vector<seadrop_t> drops(count,{{0,0,0},1.f,1.f});
  actual(&r,&sea,drops);assert(!r.locked&&r.locks==r.unlocks&&r.locks==(count+499)/500);
  int total=0;for(int n:r.batches)total+=n;assert(total==count);
  for(auto d:drops)assert(std::abs(d.fTime-.9f)<.00001f);
 }
 for(int failure:{1,2,3}){
  Renderer r;r.failAt=failure;Sea sea;std::vector<seadrop_t> drops(1001,{{0,0,0},1.f,1.f});
  actual(&r,&sea,drops);assert(!r.locked&&r.locks==r.unlocks&&r.locks==failure-1);
  assert(r.batches.size()==static_cast<size_t>(failure-1));
 }
 {Renderer r;Sea sea;std::vector<seadrop_t> drops(501,{{0,0,0},.05f,1.f});actual(&r,&sea,drops);assert(drops.empty()&&r.attempts==0);}
 {Renderer r;Sea sea;std::vector<seadrop_t> drops(500,{{0,0,0},1.f,1.f});drops.insert(drops.begin(),{{0,0,0},.05f,1.f});actual(&r,&sea,drops);assert(drops.size()==500&&r.batches==std::vector<int>{500}&&!r.locked);}
 std::cout<<"actual rain sea-drop production block: bounds/0/499/500/501/1000/expiry/first and repeated lock failure PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='rain-batch-') as d:
 c=Path(d)/'test.cpp';c.write_text(code);exe=Path(d)/'test'
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
