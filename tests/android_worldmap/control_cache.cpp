// Execute the production PCS polling wrapper against a cached physical/tree/axis
// boundary. No SDL query occurs in that boundary: this reproduces Realize-before-Update.
#include <platform/android_worldmap.hpp>
#include <platform/android_combat_input.hpp>
#include <platform/android_held_controls.hpp>
#include <controls.h>
#include <cassert>
#include <iostream>
#include <vector>
constexpr int PROP_VALUE_MAX=92,ANDROID_LOG_DEBUG=3;
int __system_property_get(const char *,char *value){*value=0;return 0;}
int __android_log_print(int,const char *,const char *,...){return 0;}
uint32_t SDL_GetTicks(){return 100;}
class PCS_CONTROLS {
public:
    int nControlsNum=3,physicalReads=0;uint32_t nFrameCounter=7;
    std::vector<USER_CONTROL> pUserControls=std::vector<USER_CONTROL>(3);
    CONTROL_STATE cache[3]={{CST_ACTIVATED,1,1},{CST_ACTIVATED,1,1},{CST_ACTIVE,0.75f,173}};
    bool GetPhysicalControlState(int32_t code,CONTROL_STATE &state){++physicalReads;state=cache[code];return true;}
    int allowed=-1; // the one control the HUD may hold in this check
    bool AndroidControlAllowed(int32_t code)const{return code==allowed;}
    int AndroidHeldInverse(int32_t)const{return -1;} // no held virtual control in this check
    bool GetControlState(int32_t,CONTROL_STATE &);
};
#include "control_poll.inc"
int main(){
    PCS_CONTROLS controls;CONTROL_STATE state{};
    for(int i=0;i<3;++i){assert(controls.GetControlState(i,state));assert(state.state==controls.cache[i].state);}
    auto &modal=storm::android::worldMapHud.modal;
    assert(modal.request({1,1,1,1},1,7,1,100));controls.physicalReads=0;
    for(int i=0;i<3;++i){assert(controls.GetControlState(i,state));assert(state.state==CST_INACTIVE&&state.lValue==0&&state.fValue==0);}
    assert(!controls.GetControlState(-1,state)&&!controls.GetControlState(3,state));
    assert(controls.physicalReads==0); // no cached activation and no subsequent virtual merge
    assert(modal.operate(1,1,1,1,1,0,100));
    assert(controls.GetControlState(0,state)&&state.state==CST_ACTIVATED&&controls.physicalReads==1);
    // a held HUD control hands out its analog part as fValue (the helm pulled 40%), a plain hold 1
    char turn[]="Ship_TurnLeft";controls.pUserControls[0].name=turn;controls.allowed=0;controls.cache[0]={CST_INACTIVE,0,0};
    auto &held=storm::android::heldControls;held.guard=[](std::string_view){return true;};held.update(5);
    const auto epoch=storm::android::combatCancelEpoch.load();
    assert(held.offer("5\tShip_TurnLeft\t1\t40",epoch));held.advance(epoch);
    assert(controls.GetControlState(0,state)&&state.state==CST_ACTIVATED&&state.lValue==1&&state.fValue==0.4f);
    assert(held.offer("5\tShip_TurnLeft\t1",epoch));held.advance(epoch);
    assert(controls.GetControlState(0,state)&&state.state==CST_ACTIVE&&state.fValue==1.0f);
    assert(held.offer("5\tShip_TurnLeft\t0",epoch));held.advance(epoch);
    assert(controls.GetControlState(0,state)&&state.state==CST_INACTIVATED&&state.lValue==0&&state.fValue==0.0f);
    std::cout<<"PASS production PCS modal fence before cached physical activation, tree and axis states; a held control's analog part\n";
}
