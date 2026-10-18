#include "platform/android_movement.hpp"
#include <cassert>
#include <limits>
using namespace storm::android;
static bool near(float a, float b) { return std::fabs(a-b)<.0001f; }
int main()
{
    MovementInput input;
    bool ready = true;
    GameplaySnapshot state; state.mode="explore"; state.inputOwner="scene:player";
    MovementPermission permission{true,true,true,true,true};
    input.guard = [&](std::string_view owner) -> std::optional<MovementPermission> {
        if (!ready || !movementOwnerCurrent(owner,state)) return std::nullopt;
        return permission;
    };
    input.updateOwner(state.inputOwner);
    auto wire = [&](uint64_t seq, std::string suffix="1\t0\t1000") {
        return "1\t"+state.inputOwner+"\t"+std::to_string(input.generation())+"\t"+std::to_string(seq)+"\t"+suffix;
    };
    assert(!input.read(0)); // legacy keyboard remains the default
    assert(input.offer(wire(1),0));
    auto axes=input.read(1); assert(axes && near(axes->y,1) && near(axes->magnitude,1));
    const auto generation=input.generation();
    state.mode="melee"; input.updateOwner(state.inputOwner);
    assert(input.generation()==generation && input.read(100)); // stance retains gesture
    assert(!input.offer(wire(1),100)); // replay
    for (const auto *bad : {"1\t1001\t0", "1\t0\t-1001", "1\t2147483648\t0", "1\t0x\t0",
                            "1\t+1\t0", "1\t1.5\t0", "0\t1\t0", "2\t0\t0", "1\t0\t0\textra"})
        assert(!input.offer(wire(2,bad),100));
    assert(!input.offer("1\twrong\t"+std::to_string(generation)+"\t2\t1\t0\t1",100));
    assert(!input.offer("1\tscene:player\t18446744073709551616\t2\t1\t0\t1",100));
    assert(input.offer(wire(2,"1\t1000\t1000"),100)); // circular clamp, no diagonal boost
    axes=input.read(100); assert(axes && near(std::hypot(axes->x,axes->y),1) && near(axes->magnitude,1));
    permission.right=false;
    axes=input.read(101); assert(axes && axes->x==0 && axes->y==1); // live lock
    permission.forward=false;
    axes=input.read(102); assert(axes && axes->magnitude==0);
    permission={true,true,true,true,true};
    assert(input.offer(wire(3,"0\t0\t0"),110)); assert(!input.read(110));
    assert(input.offer(wire(4),120));
    auto stale=wire(5);
    cancelAndroidCombatInput();
    assert(!input.read(121) && input.generation()!=generation);
    assert(!input.offer(stale,121)); // hard cancellation cannot revive from Java queue
    assert(input.offer(wire(1),130));
    auto epoch=combatCancelEpoch.load(); auto drained=wire(2);
    cancelAndroidCombatInput();
    assert(!input.offer(drained,131,epoch)); // JNI drain raced cancellation
    assert(input.offer(wire(1),200)); stale=wire(2);
    assert(input.read(550)); assert(!input.read(551));
    assert(!input.offer(stale,552)); // expired lease rotates generation
    assert(input.offer(wire(1),600)); stale=wire(2);
    assert(!input.offer(stale,951)); // late heartbeat cannot bypass expiry by arriving before read
    assert(input.offer(wire(1),1000)); stale=wire(2);
    ready=false; assert(!input.read(1001)); ready=true;
    input.updateOwner(state.inputOwner); assert(!input.offer(stale,1002));
    assert(input.offer(wire(1),1100)); stale=wire(2);
    state.inputOwner="scene:newplayer"; input.updateOwner(state.inputOwner);
    assert(!input.offer(stale,1101)); assert(!input.read(1101));
    assert(input.offer(wire(1),1200)); stale=wire(2); input.resetActivity();
    assert(!input.offer(stale,1201));
    assert(input.offer(wire(1),std::numeric_limits<uint32_t>::max()-100));
    assert(input.read(200)); assert(!input.read(251)); // tick rollover
    for (const auto *mode : {"unknown","sailing","naval"}) { state.mode=mode; assert(!movementOwnerCurrent(state.inputOwner,state)); }
    state.mode="explore"; assert(movementOwnerCurrent(state.inputOwner,state));
    assert(!movementOwnerCurrent("",state));
    assert(ordinaryMovementAllowed(true,true,false,false,false,false,false,false,true));
    assert(!ordinaryMovementAllowed(false,true,false,false,false,false,false,false,true));
    assert(!ordinaryMovementAllowed(true,false,false,false,false,false,false,false,true));
    assert(!ordinaryMovementAllowed(true,true,true,false,false,false,false,false,true));
    assert(!ordinaryMovementAllowed(true,true,false,true,false,false,false,false,true));
    assert(!ordinaryMovementAllowed(true,true,false,false,true,false,false,false,true));
    assert(!ordinaryMovementAllowed(true,true,false,false,false,true,false,false,true));
    assert(!ordinaryMovementAllowed(true,true,false,false,false,false,true,false,true));
    assert(!ordinaryMovementAllowed(true,true,false,false,false,false,false,true,true));
    assert(!ordinaryMovementAllowed(true,true,false,false,false,false,false,false,false));
    assert(movementAxes(180,0,permission).magnitude==0);
    assert(movementAxes(181,0,permission).magnitude>0);
    assert(movementAxes(121,0,permission,true).magnitude>0);
    assert(movementAxes(120,0,permission,true).magnitude==0);
    assert(near(movementAxes(0,-1000,permission).y,-1));
    bool running=false;
    float previous=-1;
    for(int i=0;i<=1000;++i) {
        auto pace=movementPace(i/1000.0f,2,5,true,running);running=pace.run;
        assert(pace.speed>=previous); previous=pace.speed;
        assert(pace.speed==0 || pace.speed==2 || pace.speed==5);
        assert(pace.run==(i>=800));
    }
    previous=5;
    for(int i=1000;i>=0;--i) {
        auto pace=movementPace(i/1000.0f,2,5,true,running);running=pace.run;
        assert(pace.speed<=previous); previous=pace.speed;
        assert(pace.run==(i>=650));
    }
    assert(!movementPace(.79f,2,5,true,false).run);
    assert(movementPace(.66f,2,5,true,true).run);
    assert(!movementPace(.64f,2,5,true,true).run);
    assert(near(movementPace(.3f,-2,-5,true).speed,2));
    assert(near(movementPace(1,-2,-5,true).speed,5));
    assert(!movementPace(1,2,5,false,true).run && near(movementPace(1,2,5,false,true).speed,2));
    // Direction is continuous, camera relative, unit length and independent of
    // the gait tier, including cameras looking at the character from the front.
    for(int camera=0;camera<360;camera+=15) for(int stick=0;stick<360;++stick) {
        const float c=camera*.017453292519943295f, a=stick*.017453292519943295f;
        MovementAxes sample{std::sin(a),std::cos(a),.5f,true};
        const auto direction=movementDirection(sample,std::sin(c),std::cos(c),0);
        assert(near(std::hypot(direction.x,direction.z),1));
        assert(near(direction.x,std::sin(c+a)) && near(direction.z,std::cos(c+a)));
    }
    const auto vertical=movementDirection({0,1,.5f,true},0,0,1.2f);
    assert(near(vertical.x,std::sin(1.2f))&&near(vertical.z,std::cos(1.2f)));
    input.resetActivity();
    assert(input.offer(wire(1,"1\t0\t200"),2000)); assert(input.read(2000)->magnitude>0);
    assert(input.offer(wire(2,"1\t0\t140"),2010)); assert(input.read(2010)->magnitude>0);
    assert(input.offer(wire(3,"1\t0\t120"),2020)); assert(input.read(2020)->magnitude==0);
    assert(input.offer(wire(4,"1\t0\t140"),2030)); assert(input.read(2030)->magnitude==0);
    assert(input.offer(wire(5,"1\t0\t200"),2040)); assert(input.read(2040)->magnitude>0);
    assert(input.offer(wire(6,"0\t0\t0"),2050)); assert(!input.read(2050));
    assert(input.offer(wire(7,"1\t0\t140"),2060)); assert(input.read(2060)->magnitude==0);
    GameplayState json; state.analogMovement=true; state.movementGeneration=123;
    json.update(state); assert(json.json().find("\"analogMovement\":true")!=std::string::npos);
    assert(json.json().find("\"movementGeneration\":123")!=std::string::npos);
}
