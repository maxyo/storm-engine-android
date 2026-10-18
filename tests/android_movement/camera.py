#!/usr/bin/env python3
"""Check real follow-camera yaw ownership; --source-root supports baseline replay."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
s=(a.source_root/'src/libs/location/src/camera_follow.cpp').read_text()
start=s.index('void CameraFollow::MoveCamera(');end=s.index('// Reinitialize camera position',start)
code=r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
struct CVECTOR {float x=0,y=0,z=0;};
struct Character {bool touch=true,locked=false;bool UsesTouchCamera()const{return touch;}void LockRotate(bool value){locked=value;}};
struct LocationCamera {float chay=0,dAy=0,lheight=1,rotInertia=8;bool isSpecialMode=false;CVECTOR pos;Character *character;};
struct CameraFollow {
 LocationCamera *lc;float camay=0,camradius=3,kRadInert=0;CVECTOR lookTo,camPos;
 void MoveCamera(float);float FindRadius(float){return 3;}void ChangeRadius(float,float){}
 void CalcPosition(float,float,float,CVECTOR&){}
};
''' + s[start:end] + r'''
int main(){
 Character actor;LocationCamera lc;lc.character=&actor;CameraFollow camera{&lc};
 // Joystick may turn the actor through every direction without orbiting the camera.
 for(int step=0;step<720;++step){lc.chay=step*.017453292519943295f;camera.MoveCamera(.016f);assert(camera.camay==0);assert(!actor.locked);}
 lc.dAy=.2f;camera.MoveCamera(.016f);assert(camera.camay>0&&!actor.locked);
 const float yaw=camera.camay;lc.dAy=0;lc.chay=3;for(int frame=0;frame<100;++frame)camera.MoveCamera(.016f);
 assert(camera.camay==yaw); // No idle recenter / rotation feedback after UP.
 actor.touch=false;lc.chay=1;camera.camay=0;camera.MoveCamera(.016f);assert(camera.camay>0&&actor.locked);
 camera.camay=0;lc.chay=0;lc.isSpecialMode=true;lc.dAy=-.1f;camera.MoveCamera(.016f);assert(camera.camay<0);
 const float old=camera.camay;camera.MoveCamera(0);assert(camera.camay==old);
 std::cout<<"PASS: actual follow camera decouples touch heading, accepts orbit, retains physical/special modes\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-movement-camera-') as d:
 cpp=Path(d)/'camera.cpp';cpp.write_text(code);exe=Path(d)/'camera'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O1','-g','-D__ANDROID__','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
