#include <platform/android_land_commands.hpp>
#include <cassert>
#include <iostream>
using namespace storm::android;
static std::string action(const LandCommandsState &s,const char *verb,const char *id="") {
    return "1\t"+std::to_string(s.sceneEpoch())+"\t"+std::to_string(s.revision())+"\t"+std::to_string(s.activityGeneration())+"\t"+verb+"\t"+id;
}
static LandCommandsFrame root(bool active=true) {
    LandCommandsFrame f;f.available=true;f.active=active;f.ownerIdentity="list1";
    if(active){f.selectedItemId="i0";f.items={{"i0","command","Travel","BI_FastReload","root:travel",true,1},{"i1","command","Talk","BI_DialogStart","root:talk",true,1},{"i2","cancel","Cancel","","cancel",true,1}};}
    return f;
}
static void accepted(LandCommandsState &s,const char *verb,const char *id,uint32_t tick) {
    assert(s.offer(action(s,verb,id),tick));auto r=s.take(tick);assert(r);s.finishAndroidOperation(*r,true,tick);
}
int main() {
    LandCommandsState s;s.resetActivity();s.update("scene1",root(false),90);
    const auto closedAck=action(s,"ack","1");accepted(s,"open","",100);s.update("scene1",root(),101);
    // Open is native-active before the first Android draw. Only a complete fresh
    // owner may suppress the strip; no drawn coverage or stale action is granted.
    assert(s.suppressStock()&&s.holdingPresentation()&&!s.covered());
    assert(!s.offer(closedAck,102));assert(s.offer(action(s,"ack","2"),110));assert(!s.covered());
    assert(s.offer(action(s,"ack","1"),120));assert(s.covered()&&!s.holdingPresentation());
    const auto oldSelectionAck=action(s,"ack","1"),oldAction=action(s,"activateItem","i0");
    accepted(s,"selectItem","i1",200);auto f=root();f.selectedItemId="i1";s.update("scene1",f,201);
    assert(!s.offer(oldSelectionAck,202)&&!s.offer(oldAction,202));
    assert(s.suppressStock()&&!s.covered());assert(s.offer(action(s,"ack","2"),220));
    // A slow draw completes normally: the hold waits for the answer, not for a clock.
    assert(s.suppressStock()&&s.holdingPresentation()&&s.handoffWaiting(1199)==999);
    assert(s.offer(action(s,"ack","1"),1199));assert(s.covered());
    accepted(s,"selectItem","i0",1200);f.selectedItemId="i0";s.update("scene1",f,1201);assert(s.suppressStock());
    assert(s.offer(action(s,"ack","1"),1203));
    accepted(s,"activateItem","i0",1300);f.stage="travel";f.pendingLabel="Travel";
    f.items[0]={"i0","travel","Actual destination","fr_1","reload0:actual-location:actual-locator",true,1};
    s.update("scene1",f,1301);assert(s.suppressStock()&&!s.covered());
    assert(s.offer(action(s,"ack","1"),1303));
    accepted(s,"back","",1400);s.update("scene1",root(),1401);assert(s.suppressStock());
    // A covered menu that changes keeps stock off until the sheet answers, even when
    // a frame on a slow device takes seconds (10 fps emulator: answers came 1.3 s late).
    assert(s.offer(action(s,"ack","1"),1403));
    auto changed=root();changed.items[1].enabled=false;s.update("scene1",changed,1800);
    assert(s.holdingPresentation()&&!s.covered());auto oldAck=action(s,"ack","1");
    assert(s.offer(action(s,"ack","2"),2399));assert(s.holdingPresentation()); // taking it, not drawn yet
    assert(s.suppressStock()&&s.handoffWaiting(9800)==8000&&s.offer(action(s,"activateItem","i0"),9800)&&s.take(9800));
    s.invalidate(true);assert(!s.offer(oldAck,9801)); // only the current revision answers
    assert(s.offer(action(s,"ack","1"),9802)&&s.covered()&&!s.holdingPresentation());
    // A revision the sheet will not draw is declined: stock returns at once.
    changed.items[1].enabled=true;s.update("scene1",changed,9900);assert(s.holdingPresentation());
    assert(s.offer(action(s,"ack","0"),9901)&&!s.suppressStock());
    s.update("scene1",root(false),9902); // the player closed the stock menu
    // Real release is distinct from pending: restore fallback immediately.
    accepted(s,"open","",2500);s.update("scene1",root(),2501);assert(s.holdingPresentation());
    assert(s.offer(action(s,"ack","0"),2502));assert(!s.suppressStock()&&s.handoffWaiting(4000)==0);
    assert(s.offer(action(s,"ack","1"),2503));accepted(s,"selectItem","i1",2600);
    auto incompatible=root();incompatible.complete=false;s.update("scene1",incompatible,2601);assert(!s.suppressStock());
    // Scene, Activity and actual command-list replacement never inherit a hold.
    s.update("scene1",root(),2700);accepted(s,"selectItem","i1",2701);auto stale=action(s,"ack","1");
    s.update("scene2",root(),2702);assert(!s.suppressStock()&&!s.offer(stale,2702));
    accepted(s,"selectItem","i1",2800);stale=action(s,"ack","1");auto replacement=root();replacement.ownerIdentity="list2";
    s.update("scene2",replacement,2801);assert(!s.suppressStock()&&!s.offer(stale,2801));
    accepted(s,"selectItem","i1",2900);stale=action(s,"ack","1");s.resetActivity();assert(!s.suppressStock()&&!s.offer(stale,2901));
    // Request failure and explicit Close do not start a visual hold.
    LandCommandsRequest rejected{s.sceneEpoch(),s.revision(),s.activityGeneration(),"selectItem","i0",3000};
    s.finishAndroidOperation(rejected,false,3000);assert(!s.suppressStock());
    accepted(s,"close","",3100);assert(!s.suppressStock());
    // The waiting time that reports a silent UI survives unsigned clock rollover.
    s.update("wrap",root(false),0xfffffff0u);accepted(s,"open","",0xfffffff0u);s.update("wrap",root(),0xfffffff1u);
    assert(s.holdingPresentation()&&s.handoffWaiting(0x3d8u)==0x3e8u);
    std::cout<<"PASS: initial-open/select/child/back presentation continuity; no stale ACK/action authority; holds that wait for the UI answer however slow its frames, decline, release/incompatible/scene/activity/list invalidation and clock rollover\n";
}
