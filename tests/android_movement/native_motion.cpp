#include "platform/android_movement.hpp"
#include "storm_glm.h"
#include <cassert>
#include <iostream>
using namespace storm::android;
// The production fragments compute with GLM vectors directly.
using Vec3 = glm::vec3;
bool near(float a,float b) { return std::fabs(a-b)<.00001f; }
void same(Vec3 a,Vec3 b) { assert(near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z)); }
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
struct Camera {
    enum {cwm_none, cwm_follow, cwm_topos, cwm_free};
    int wmode=cwm_follow;
    bool isLookMode=false,isLockFPMode=false,isSleep=false,m_bTrackMode=false;
    Vec3 lookTo{0,0,1},camPos{},pos{},fromLook{};
    float lheight=1;
    void ExecuteTopos(float);
    mutable Vec3 movementForward{0,0,1};
#include "camera_interface.inc"
} camera;
#include "camera_topos.inc"
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
    Vec3 androidMotionDirection{1,0,0},curPos,impulse;
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
    float turnX=0,turnZ=0; int turns=0;
    void Turn(float x,float z){turnX=x;turnZ=z;++turns;}
    void StartMove(bool back){isMove=true;lastBack=back;}
    void StrafeWhenMove(float);
    void StrafeWhenStop(float);
    void Recoil(){++recoils;if(allowDodge)fgtSetType=1;}
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
    // Swimming: the held stick steers the native swim, relative to the camera, through every
    // change between walking and swimming (it used to be dropped in water: the waves carried
    // the hero one way and the stick could not change it).
    {
        Player swimmer; swimmer.androidCameraRelative=true; camera.lookTo={0,0,1};
        swimmer.isSwim=true; swimmer.isMove=false; swimmer.androidRunTier=true;
        swimmer.analog(MovementAxes{1,0,1,true}); // pushed right
        assert(swimmer.isMove && !swimmer.lastRun && !swimmer.androidRunTier && swimmer.strafeMove==0 && !swimmer.androidMotion);
        assert(swimmer.turns==1 && near(swimmer.turnX,1) && near(swimmer.turnZ,0));
        swimmer.analog(MovementAxes{0,-1,1,true}); // pulled back: swims towards the camera
        assert(swimmer.turns==2 && near(swimmer.turnX,0) && near(swimmer.turnZ,-1));
        swimmer.isSwim=false; swimmer.analog(MovementAxes{0,1,1,true}); // ashore: the touch walk again
        assert(swimmer.androidMotion && swimmer.turns==2 && near(swimmer.androidMotionDirection.z,1));
        swimmer.isSwim=true; swimmer.analog(MovementAxes{-1,0,1,true}); // back in the water, same finger
        assert(swimmer.turns==3 && near(swimmer.turnX,-1) && swimmer.isMove);
        swimmer.analog(MovementAxes{0,0,0,true}); // the thumb in the dead zone stops the swim
        assert(!swimmer.isMove && swimmer.androidMotionStop);
        assert(touchGait(MovementAxes{0,-1,1,true},true,true)==TouchGait::swim && touchGait(MovementAxes{0,-1,1,true},false,true)==TouchGait::combatBack &&
               touchGait(MovementAxes{1,0,1,true},false,true)==TouchGait::combatSide && touchGait(MovementAxes{0,1,1,true},false,false)==TouchGait::walk &&
               touchGait(MovementAxes{},true,false)==TouchGait::stop);
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
    Vec3 expected(sinf(physical.ay)*.2f,0,cosf(physical.ay)*.2f);
    expected+=Vec3(cosf(physical.ay),0,-sinf(physical.ay))*.045f;
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
        camera.lookTo={std::sin(c),0,std::cos(c)};
        Player moving;
        moving.analog({std::sin(a),std::cos(a),.5f,true});moving.step();
        assert(!moving.lastBack&&!moving.lastRun);
        assert(near(std::hypot(moving.curPos.x,moving.curPos.z),.2f));
        assert(near(std::sin(moving.ay),moving.androidMotionDirection.x));
        assert(near(std::cos(moving.ay),moving.androidMotionDirection.z));
        assert(moving.strafeMove==0);
    }
    // The same held stick follows the live locator camera as the actor moves
    // across the location. Actor heading changes must never feed back into its basis.
    for(int mode : {Camera::cwm_topos, Camera::cwm_follow})
    {
        camera.wmode=mode;
        Player moving;
        MovementInput held;
        held.guard=[](std::string_view){return std::optional<MovementPermission>({true,true,true,true,true});};
        held.updateOwner("camera-test");
        assert(held.offer("1\tcamera-test\t"+std::to_string(held.generation())+"\t1\t1\t500\t0"));
        for(int frame=0;frame<360;++frame)
        {
            const float yaw=frame*.017453292519943295f;
            camera.lookTo={std::sin(yaw)*3, -4, std::cos(yaw)*3};
            if(mode==Camera::cwm_topos)
            {
                camera.pos=camera.lookTo-Vec3(0,camera.lheight,0);
                camera.ExecuteTopos(.1f); // real locator camera tracks the actor position
            }
            const Vec3 before=moving.curPos;
            moving.analog(*held.read());moving.step(); // no new pointer/MOVE packet
            assert(moving.androidCameraRelative && "live locator camera must own movement basis");
            same(moving.curPos-before,Vec3(std::cos(yaw)*.2f,0,-std::sin(yaw)*.2f));
            assert(moving.androidCameraRelative && !moving.lastBack && moving.strafeMove==0);
        }
        // A pitched/vertical view keeps the last camera heading independently
        // of character yaw; leaving the pole picks up the new camera immediately.
        camera.lookTo={3,-4,0};moving.analog({0,1,.5f,true});moving.step();
        for(Vec3 pole : {Vec3(0,-10,0),Vec3(.00001f,-10,.00001f)})
        {
            camera.lookTo=pole;moving.ay=2;
            moving.analog({1,0,.5f,true});
            same(moving.androidMotionDirection,{0,0,-1});
        }
        camera.lookTo={0,-4,-3};moving.analog({0,1,.5f,true});
        same(moving.androidMotionDirection,{0,0,-1});
        moving.release(std::nullopt);moving.androidMotion=false;
        const Vec3 stopped=moving.curPos;
        for(int frame=0;frame<10;++frame){moving.step();same(moving.curPos,stopped);}
        Player swimmer;swimmer.isSwim=true;swimmer.analog({0,1,.5f,true});
        assert(near(swimmer.turnX,0)&&near(swimmer.turnZ,-1));
    }
    // A first-person flag left over from the previous follow mode must not
    // disable the now-visible locator camera. Free/track/sleep never opt in.
    camera.wmode=Camera::cwm_topos;camera.isLookMode=true;
    Player locator;locator.analog({0,1,.5f,true});assert(locator.androidCameraRelative);
    camera.isLookMode=false;
    for(int excluded=0;excluded<3;++excluded)
    {
        camera.wmode=excluded==0?Camera::cwm_free:Camera::cwm_follow;
        camera.m_bTrackMode=excluded==1;camera.isSleep=excluded==2;
        Player disabled;disabled.analog({0,1,.5f,true});assert(!disabled.androidCameraRelative);
    }
    camera.m_bTrackMode=camera.isSleep=false;
    camera.wmode=Camera::cwm_follow;
    camera.lookTo={0,0,-1};
    Player combat;combat.fight=true;combat.ay=0;
    combat.analog({0,1,.5f,true});combat.step();
    assert(combat.curPos.z>0&&combat.curPos.x==0&&!combat.lastBack);
    camera.lookTo={0,0,1};
    // First-person uses actor-relative forward/back and preserves look yaw.
    camera.isLookMode=true;camera.lookTo={0,0,-1};
    Player eyes;eyes.ay=0;eyes.analog({0,-1,.5f,true});eyes.step();
    assert(!eyes.androidCameraRelative&&eyes.lastBack&&eyes.ay==0&&eyes.curPos.z<0);
    eyes.analog({1,0,.5f,true});eyes.step();
    assert(eyes.ay==0&&eyes.androidMotionDirection.x==1&&eyes.androidMotionDirection.z==0);
    camera.isLookMode=false;camera.lookTo={0,0,1};
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
