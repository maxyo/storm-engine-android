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
    assert(!input.read()); // legacy keyboard remains the default
    assert(input.offer(wire(1)));
    auto axes=input.read(); assert(axes && near(axes->y,1) && near(axes->magnitude,1));
    const auto generation=input.generation();
    state.mode="melee"; input.updateOwner(state.inputOwner);
    assert(input.generation()==generation && input.read()); // stance retains gesture
    assert(!input.offer(wire(1))); // replay
    for (const auto *bad : {"1\t1001\t0", "1\t0\t-1001", "1\t2147483648\t0", "1\t0x\t0",
                            "1\t+1\t0", "1\t1.5\t0", "0\t1\t0", "2\t0\t0", "1\t0\t0\textra"})
        assert(!input.offer(wire(2,bad)));
    assert(!input.offer("1\twrong\t"+std::to_string(generation)+"\t2\t1\t0\t1"));
    assert(!input.offer("1\tscene:player\t18446744073709551616\t2\t1\t0\t1"));
    assert(input.offer(wire(2,"1\t1000\t1000"))); // circular clamp, no diagonal boost
    axes=input.read(); assert(axes && near(std::hypot(axes->x,axes->y),1) && near(axes->magnitude,1));
    permission.right=false;
    axes=input.read(); assert(axes && axes->x==0 && axes->y==1); // live lock
    permission.forward=false;
    axes=input.read(); assert(axes && axes->magnitude==0);
    permission={true,true,true,true,true};
    assert(input.offer(wire(3,"0\t0\t0"))); assert(!input.read());
    assert(input.offer(wire(4)));
    auto stale=wire(5);
    cancelAndroidCombatInput();
    assert(!input.read() && input.generation()!=generation);
    assert(!input.offer(stale)); // hard cancellation cannot revive from Java queue
    assert(input.offer(wire(1)));
    auto epoch=combatCancelEpoch.load(); auto drained=wire(2);
    cancelAndroidCombatInput();
    assert(!input.offer(drained,epoch)); // JNI drain raced cancellation
    // Event model: a held stick stays held however long the UI is silent (slow frames, seconds
    // apart); only its release, a cancellation, a new owner or the guard end it.
    assert(input.offer(wire(1)));
    for (int frame=0; frame<1000; ++frame) { axes=input.read(); assert(axes && near(axes->magnitude,1)); }
    assert(input.offer(wire(2))); // the next change, seconds later, still belongs to the same gesture
    assert(input.offer(wire(3,"0\t0\t0")) && !input.read());
    assert(input.offer(wire(4))); stale=wire(5);
    ready=false; assert(!input.read()); ready=true;
    input.updateOwner(state.inputOwner); assert(!input.offer(stale));
    assert(input.offer(wire(1))); stale=wire(2);
    state.inputOwner="scene:newplayer"; input.updateOwner(state.inputOwner);
    assert(!input.offer(stale)); assert(!input.read());
    assert(input.offer(wire(1))); stale=wire(2); input.resetActivity();
    assert(!input.offer(stale));
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
    assert(input.offer(wire(1,"1\t0\t200"))); assert(input.read()->magnitude>0);
    assert(input.offer(wire(2,"1\t0\t140"))); assert(input.read()->magnitude>0);
    assert(input.offer(wire(3,"1\t0\t120"))); assert(input.read()->magnitude==0);
    assert(input.offer(wire(4,"1\t0\t140"))); assert(input.read()->magnitude==0);
    assert(input.offer(wire(5,"1\t0\t200"))); assert(input.read()->magnitude>0);
    assert(input.offer(wire(6,"0\t0\t0"))); assert(!input.read());
    assert(input.offer(wire(7,"1\t0\t140"))); assert(input.read()->magnitude==0);
    GameplayState json; state.analogMovement=true; state.movementGeneration=123;
    json.update(state); assert(json.json().find("\"analogMovement\":true")!=std::string::npos);
    assert(json.json().find("\"movementGeneration\":123")!=std::string::npos);
}
