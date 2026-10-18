// Execute the real engine callback, with the synchronous AppState/audio boundary
// advancing the clock. A focus event must reach the gate before either can block.
#include <platform/android_worldmap.hpp>
#include <os_window.hpp>
#include <cassert>
#include <iostream>
uint32_t now=100,appDelay=0,audioDelay=0;
uint32_t SDL_GetTicks(){return now;}
namespace storm::android {void traceWorldMapModal(const char *,const std::string &) {}}
bool bActive=true,isRunning=true,bSoundInBackground=false;
struct VSoundService {
    void SetActiveWithFade(bool active){if(!active){now+=audioDelay;storm::android::worldMapHud.modal.ownsInput(now);}}
} audio;
struct CoreBoundary {
    bool initialized(){return true;}
    void Event(const char *){}
    void AppState(bool active){if(!active){now+=appDelay;storm::android::worldMapHud.modal.ownsInput(now);}}
    void *GetService(const char *){return &audio;}
} core;
auto *core_private=&core;
#include "window_focus.inc"
int main(){
    auto &modal=storm::android::worldMapHud.modal;
    for(auto delay:{500u,563u,1200u})for(bool appBlocks:{false,true}){
        modal.owner({});now=100;bActive=true;
        assert(modal.request({1,1,1,1},1,7,1,now));modal.presented({1,1,1,1},7,1,now);
        assert(modal.operate(1,1,1,1,1,1,now));
        now=150;appDelay=appBlocks?delay:0;audioDelay=appBlocks?0:delay;
        HandleWindowEvent(storm::OSWindow::FocusLost);
        assert(!bActive&&now==150+delay&&modal.hold(false,now)&&modal.receipt(now).failed==0);
        assert(modal.operate(1,1,1,1,1,4,now)); // normal Close remains available after the fade
    }
    modal.owner({});now=100;assert(modal.request({1,1,1,1},1,7,1,now));modal.presented({1,1,1,1},7,1,now);
    assert(modal.operate(1,1,1,1,1,1,now));now=600;appDelay=audioDelay=0;
    HandleWindowEvent(storm::OSWindow::FocusLost);
    assert(modal.receipt(now).failed==1); // a genuinely late event does not extend the500ms grace
    std::cout<<"PASS production focus callback before500–1200ms AppState/audio stalls; truly late focus still refuses admission\n";
}
