// Execute the production PCS polling wrapper against a cached physical/tree/axis
// boundary. No SDL query occurs in that boundary: this reproduces Realize-before-Update.
#include <platform/android_worldmap.hpp>
#include <platform/android_combat_input.hpp>
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
    bool AndroidControlAllowed(int32_t)const{return false;}
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
    std::cout<<"PASS production PCS modal fence before cached physical activation, tree and axis states\n";
}
