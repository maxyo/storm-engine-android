#include "platform/android_movement.hpp"
#include <cassert>
#include <iostream>
using namespace storm::android;
struct CVECTOR
{
    float x=0,y=0,z=0;
    CVECTOR(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    CVECTOR operator*(float k) const { return {x*k,y*k,z*k}; }
    CVECTOR operator-() const { return {-x,-y,-z}; }
    CVECTOR operator+(CVECTOR v) const { return {x+v.x,y+v.y,z+v.z}; }
    CVECTOR &operator*=(float k) { x*=k;y*=k;z*=k;return *this; }
    CVECTOR &operator+=(CVECTOR v) { x+=v.x;y+=v.y;z+=v.z;return *this; }
};
bool near(float a,float b) { return std::fabs(a-b)<.00001f; }
void same(CVECTOR a,CVECTOR b) { assert(near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z)); }
constexpr int fgt_none=0,npct_none=0,aicmd_none=0,UCT_ControlTree=1,UNASSIGNED_CONTROL=-1;
struct USER_CONTROL { const char *name; bool bLocked=false,bSystemLocked=false; int control_type=0,system_code=0; };
struct CONTROL_STATE { int lValue=0; };
struct Controls
{
    int left=0,right=0;
    bool GetControlState(const char *name,CONTROL_STATE &state) { state.lValue=std::string_view(name)=="ChrStrafeLeft"?left:right;return true; }
    std::vector<USER_CONTROL> entries;
    int reads=0;
    int GetControlsNum(){return entries.size();}
    bool GetControlDesc(int i,USER_CONTROL &out){++reads;out=entries.at(i);return true;}
} controls;
struct Core { struct Controls *Controls=&controls; unsigned GetEntityId(const char*) {return 7;} } core;
MovementPermission permissions(const GameplaySnapshot &current)
{
#include "permissions.inc"
}
struct CMatrix { CVECTOR Vz(){return {0,0,1};} void Transposition3X3(){} };
struct Camera { bool thirdPerson=true; CVECTOR forward{0,0,1};
    CVECTOR MovementViewDirection()const{return forward;} bool IsThirdPersonFollow()const{return thirdPerson;} } camera;
constexpr int D3DTS_VIEW=0;
struct Renderer { void GetTransform(int,CMatrix&){} } renderer;
struct Location { Renderer *GetRS(){return &renderer;} } location;
struct Motion
{
    bool fight=false,isTurnLock=false,androidCameraRelative=false;
    bool IsFight(){return fight;}
    bool UsesTouchCamera()const{return androidCameraRelative&&!fight&&!isSwim&&!isJump;}
    float nay=0,turnDir=0;
    bool androidMotion=true,androidMotionStop=false,androidMotionApplied=false,isMove=true,lockMove=false,isJump=false,isSwim=false;
    struct { const char *name=nullptr; } priorityAction;
    int fgtCurType=fgt_none,fgtSetType=fgt_none;
    float androidMotionSpeed=.2f,androidMotionWalkSpeed=2, speed=2,dltTime=.1f,k=1,ay=.7f,strafeMove=.5f,strafeVel=.5f;
    struct Action {float speed=2;} walk,stsUp,stsDown,stsUpBack,stsDownBack;
    Action backwalk{-2},run{5},backrun{-5},fightwalk{2},fightrun{5};
    Action *curMove=&walk;
    CVECTOR androidMotionDirection{1,0,0},curPos,impulse;
    void step()
    {
#include "motion.inc"
#include "displacement.inc"
#include "strafe.inc"
    }
};
struct Player:Motion
{
    struct {int task=0;} task;
    struct {int cmd=0;} command;
    bool androidMovementHeld=true,androidRunTier=false;
    uint64_t androidCameraGeneration=0;
    Camera *locCam=&camera;
    bool lastBack=false,lastRun=false;
    bool nativeForward=false,nativeBack=false;
    bool GoForward(float){return nativeForward;}
    bool GoBack(float){return nativeBack;}
    bool IsRunMode(float){return true;}
    bool isSpecialMode=false,allowDodge=true,isRunDisable=false; int dodges=0,recoils=0;
    void StrafeLeft(){++dodges;if(allowDodge)fgtSetType=1;}
    void StrafeRight(){++dodges;if(allowDodge)fgtSetType=1;}
    void SetRunMode(bool run){lastRun=run;}
    void StartMove(bool back){isMove=true;lastBack=back;}
    void StrafeWhenMove(float);
    void StrafeWhenStop(float);
    void Recoil(){++recoils;if(allowDodge)fgtSetType=1;}
    Location *GetLocation(){return &location;}
    void StopAndroidMovement();
    void analog(MovementAxes sample)
    {
        std::optional<MovementAxes> androidAxes=sample;
        androidMotion=false; // the real Move clears the per-frame vector first
        release(androidAxes);
#include "analog_decision.inc"
    }
    void physical()
    {
#include "physical.inc"
    }
    unsigned GetId(){return 7;}
    void StopMove(){if(!priorityAction.name)isMove=false;}
    void release(std::optional<MovementAxes> androidAxes)
    {
#include "release.inc"
    }
};
#include "strafe_controls.inc"
#include "stop.inc"
int main()
{
    Player slow;
    slow.step(); same(slow.curPos,{.02f,0,0}); // full native speed=2, touch target=.2
    slow.release(std::nullopt); slow.androidMotion=false;
    assert(slow.androidMotionStop&&!slow.isMove);
    auto stopped=slow.curPos;
    for(int frame=0;frame<4;++frame){slow.speed-=.2f;slow.step();same(slow.curPos,stopped);}
    assert(slow.speed>.1f); // suppress the residual, never mutate native blending
    slow.curMove=nullptr;slow.step();same(slow.curPos,stopped); // idle animation after release
    slow.impulse={.4f,0,-.3f};slow.step();same(slow.curPos,{.06f,0,-.03f});
    Player center; center.release(MovementAxes{0,0,0,true});center.androidMotion=false;
    center.step();same(center.curPos,{});assert(center.androidMotionStop);
    // Special actions retain their original native displacement and impulse,
    // even if a stop was latched immediately before that action began.
    for(int special=0;special<5;++special)
    {
        Motion native,touch;
        native.androidMotion=false;
        touch.androidMotion=false;touch.androidMotionStop=true;touch.isMove=false;
        native.isMove=false;
        native.impulse=touch.impulse={.4f,0,-.3f};
        if(special==0)native.priorityAction.name=touch.priorityAction.name="hit";
        if(special==1)native.fgtCurType=touch.fgtCurType=1;
        if(special==2)native.fgtSetType=touch.fgtSetType=1;
        if(special==3)native.isSwim=touch.isSwim=true;
        if(special==4)native.isJump=touch.isJump=true;
        native.step();touch.step();same(native.curPos,touch.curPos);assert(!touch.androidMotionApplied);
    }
    Player scripted;scripted.androidMotionStop=true;scripted.androidRunTier=true;
    scripted.androidCameraRelative=true;scripted.task.task=1;scripted.release(std::nullopt);
    assert(!scripted.androidMotionStop&&!scripted.androidRunTier&&!scripted.androidCameraRelative);
    scripted.task.task=0;scripted.command.cmd=1;scripted.androidMotionStop=true;scripted.release(std::nullopt);
    assert(!scripted.androidMotionStop);
    // A physical movement request clears the stop latch and resumes the exact
    // untouched native displacement route (including its strafe contribution).
    Player physical;physical.androidMotion=false;physical.androidMotionStop=true;
    physical.nativeForward=true;physical.physical();assert(!physical.androidMotionStop);
    physical.androidMotionStop=true;physical.nativeForward=false;physical.nativeBack=true;
    physical.physical();assert(!physical.androidMotionStop);physical.step();
    CVECTOR expected(sinf(physical.ay)*.2f,0,cosf(physical.ay)*.2f);
    expected+=CVECTOR(cosf(physical.ay),0,-sinf(physical.ay))*.045f;
    same(physical.curPos,expected);
    for(bool left : {false,true})
    {
        Player lateral;lateral.androidMotion=false;lateral.androidMotionStop=true;
        controls.left=left?1:0;controls.right=left?0:1;
        lateral.physical();assert(!lateral.androidMotionStop);
        assert(lateral.strafeMove==0); // stopped explore strafe keeps its native semantics
        lateral.step();same(lateral.curPos,expected); // native residual strafeVel survives
        Player fight;fight.androidMotion=false;fight.androidMotionStop=true;fight.fight=true;
        fight.physical();assert(!fight.androidMotionStop&&fight.dodges==1&&fight.fgtSetType==1);
        fight.step();assert(!fight.androidMotionApplied); // native dodge is not a touch stop
    }
    controls.left=controls.right=0;
    // Exercise the entire production analog decision block: slow forward
    // transitions to held low-radius side/back input, not just UP/center.
    for(float x : {-1.0f,0.0f,1.0f})
    {
        Player melee;melee.fight=true;
        melee.analog({0,1,.1f,true});melee.step();auto before=melee.curPos;
        melee.analog({x,x==0?-1.0f:0.0f,.1f,true});melee.step();
        same(melee.curPos,before);assert(melee.androidMotionStop&&!melee.isMove);
        assert(melee.dodges==0&&melee.recoils==0);
        // A high-radius special denied by native cooldown/energy must also
        // remain stopped; accepting it must leave native combat physics alone.
        melee.allowDodge=false;
        melee.analog({x,x==0?-1.0f:0.0f,.8f,true});melee.step();same(melee.curPos,before);
        assert(melee.dodges+melee.recoils==1);
        melee.allowDodge=true;
        melee.analog({x,x==0?-1.0f:0.0f,.8f,true});melee.step();
        assert(melee.fgtSetType==1&&!melee.androidMotionApplied);
    }
    Player analogCenter;analogCenter.analog({0,1,.1f,true});analogCenter.step();
    auto beforeCenter=analogCenter.curPos;analogCenter.analog({0,0,0,true});analogCenter.step();
    same(analogCenter.curPos,beforeCenter);assert(analogCenter.androidMotionStop);
    // Real Player decision + Character displacement: a front-facing camera
    // or a diagonal may not produce backwards gait / motion against heading.
    for(int cam=0;cam<360;cam+=30) for(int angle=0;angle<360;angle+=5)
    {
        const float c=cam*.017453292519943295f,a=angle*.017453292519943295f;
        camera.forward={std::sin(c),0,std::cos(c)};
        Player moving;
        moving.analog({std::sin(a),std::cos(a),.5f,true});moving.step();
        assert(!moving.lastBack&&!moving.lastRun);
        assert(near(std::hypot(moving.curPos.x,moving.curPos.z),.2f));
        assert(near(std::sin(moving.ay),moving.androidMotionDirection.x));
        assert(near(std::cos(moving.ay),moving.androidMotionDirection.z));
        assert(moving.strafeMove==0);
    }
    camera.forward={0,0,-1};
    Player combat;combat.fight=true;combat.ay=0;
    combat.analog({0,1,.5f,true});combat.step();
    assert(combat.curPos.z>0&&combat.curPos.x==0&&!combat.lastBack);
    camera.forward={0,0,1};
    // First-person uses actor-relative forward/back and preserves look yaw.
    camera.thirdPerson=false;camera.forward={0,0,-1};
    Player eyes;eyes.ay=0;eyes.analog({0,-1,.5f,true});eyes.step();
    assert(!eyes.androidCameraRelative&&eyes.lastBack&&eyes.ay==0&&eyes.curPos.z<0);
    eyes.analog({1,0,.5f,true});eyes.step();
    assert(eyes.ay==0&&eyes.androidMotionDirection.x==1&&eyes.androidMotionDirection.z==0);
    camera.thirdPerson=true;camera.forward={0,0,1};
    Player tiers;
    for(float radius : {.2f,.4f,.79f}) {tiers.analog({0,1,radius,true});assert(tiers.androidMotionSpeed==2&&!tiers.lastRun);}
    for(float radius : {.8f,1.f,.7f,.65f}) {tiers.analog({0,1,radius,true});assert(tiers.androidMotionSpeed==5&&tiers.lastRun);}
    tiers.analog({0,1,.64f,true});assert(tiers.androidMotionSpeed==2&&!tiers.lastRun);
    tiers.analog({0,1,1,true});tiers.analog({0,0,0,true});assert(!tiers.androidRunTier);
    tiers.analog({0,1,.7f,true});assert(tiers.androidMotionSpeed==2&&!tiers.lastRun);
    GameplaySnapshot snapshot;snapshot.capabilities={"ChrForward","ChrBackward","ChrStrafeLeft","ChrStrafeRight","ChrRun"};
    controls.entries={{"irrelevant"},{"ChrForward"},{"ChrBackward",true},{"ChrStrafeLeft",false,true},
        {"ChrStrafeRight",false,false,0,UNASSIGNED_CONTROL},{"ChrRun",false,false,UCT_ControlTree,UNASSIGNED_CONTROL},
        {"unused-after-all-five"},{"ChrBackward"}};
    auto allowed=permissions(snapshot);
    assert(allowed.forward&&!allowed.backward&&!allowed.left&&!allowed.right&&allowed.run);
    assert(controls.reads==6); // stop after all five names, including locked/unmapped
    controls.reads=0;snapshot.capabilities={"ChrBackward"};allowed=permissions(snapshot);
    assert(!allowed.forward&&!allowed.backward&&!allowed.left&&!allowed.right&&!allowed.run);
    assert(controls.reads==6);
    // A duplicate name cannot count as another resolved direction, and a later
    // unlocked duplicate cannot override the first locked native descriptor.
    controls.entries={{"ChrForward",true},{"ChrForward"},{"ChrBackward"},{"ChrStrafeLeft"},{"ChrRun"},{"unrelated"}};
    controls.reads=0;snapshot.capabilities={"ChrForward","ChrBackward","ChrStrafeLeft","ChrRun"};
    allowed=permissions(snapshot);
    assert(!allowed.forward&&allowed.backward&&allowed.left&&!allowed.right&&allowed.run);
    assert(controls.reads==int(controls.entries.size()));
    std::cout<<"PASS actual Player release and Character displacement: no lurch with residual speed, idle/null animation, impulses, specials, physical fallback; one-pass descriptor locks\n";
}
