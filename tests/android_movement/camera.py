#!/usr/bin/env python3
"""Check the real follow camera (mobile camera mode) and its drag yaw; --source-root supports baseline replay."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
s=(a.source_root/'src/libs/location/src/camera_follow.cpp').read_text()
start=s.index('void CameraFollow::MoveCamera(');end=s.index('// Reinitialize camera position',start)
code=r'''
#include <cassert>
#include <cmath>
#include <platform/android_camera.hpp>
#include <cstdint>
#include <iostream>
struct Vec3 {float x=0,y=0,z=0;};
struct Character {bool fight=false,locked=false;bool MobileCameraOrbit()const{return !fight;}void LockRotate(bool value){locked=value;}};
struct LocationCamera {float chay=0,dAy=0,dragAy=0,lheight=1,rotInertia=8;bool isSpecialMode=false;Vec3 pos;Character *character;};
struct CameraFollow {
 LocationCamera *lc;float camay=0,camradius=3,kRadInert=0;Vec3 lookTo,camPos;
 void MoveCamera(float);float FindRadius(float){return 3;}void ChangeRadius(float,float){}
 void CalcPosition(float,float,float,Vec3&){}
};
''' + s[start:end] + r'''
int main(){
 Character actor;LocationCamera lc;lc.character=&actor;CameraFollow camera{&lc};
 using storm::android::mobileCameraYawStep;
 // Mobile camera mode: the stick may turn the hero through every direction; the camera never
 // swings behind him (no auto-rotation) and never locks his rotation.
 for(int step=0;step<720;++step){lc.chay=step*.017453292519943295f;camera.MoveCamera(.016f);assert(camera.camay==0);assert(!actor.locked);}
 // A drag turns the camera by all of it, every frame, whatever the frame time (the touch port's
 // old orbit eased by dltTime*rotInertia and dropped the rest: about 1/24 at 60 fps).
 for(float dt:{.016f,.1f,1.f}){const float before=camera.camay;lc.dragAy=mobileCameraYawStep(20);camera.MoveCamera(dt);
   assert(std::fabs(camera.camay-before-.2f)<1e-5f&&!actor.locked);}
 const float yaw=camera.camay;lc.dragAy=0;lc.chay=3;for(int frame=0;frame<100;++frame)camera.MoveCamera(.016f);
 assert(camera.camay==yaw); // no idle recentring behind the hero after the finger lifts
 // Native angle per drag count (the fight turn), both ways, bounded per frame.
 assert(std::fabs(mobileCameraYawStep(10)-.1f)<1e-6f&&std::fabs(mobileCameraYawStep(-10)+.1f)<1e-6f);
 assert(mobileCameraYawStep(1000)==.3f&&mobileCameraYawStep(-1000)==-.3f&&mobileCameraYawStep(0)==0);
 // A fight keeps the native follow camera: it swings behind the fighter and locks his rotation.
 actor.fight=true;lc.chay=1;camera.camay=0;camera.MoveCamera(.016f);assert(camera.camay>0&&actor.locked);
 // Freelook (special mode) keeps the native orbit by dAy.
 actor.fight=false;actor.locked=false;camera.camay=0;lc.chay=0;lc.isSpecialMode=true;lc.dAy=-.1f;camera.MoveCamera(.016f);assert(camera.camay<0);
 const float old=camera.camay;camera.MoveCamera(0);assert(camera.camay==old);
 std::cout<<"PASS: mobile camera mode turns only by the whole drag, no auto-rotation or rotation lock; fight and freelook keep the native camera\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-movement-camera-') as d:
 cpp=Path(d)/'camera.cpp';cpp.write_text(code);exe=Path(d)/'camera'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O1','-g','-D__ANDROID__','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(a.source_root/'src/libs/util/include'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
