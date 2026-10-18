#include <platform/android_land_commands.hpp>
#include <cassert>
#include <iostream>
using namespace storm::android;
static std::string action(const LandCommandsState &s,const char *verb,const char *id="") {
    return "1\t"+std::to_string(s.sceneEpoch())+"\t"+std::to_string(s.revision())+"\t"+std::to_string(s.activityGeneration())+"\t"+verb+"\t"+id;
}
static LandCommandsFrame frame() {
    LandCommandsFrame f;f.available=true;f.active=true;f.ownerIdentity="actual-native-list";f.selectedItemId="i0";
    f.items={{"i0","command","Talk","BI_DialogStart","target:actual-character",true,1},{"i1","cancel","Cancel","","cancel",true,1}};return f;
}
int main() {
    LandCommandsState s;s.resetActivity();
    // Record the native Close boundary: stock was opened independently and the
    // Android adapter has never supplied a usable snapshot or taken ownership.
    bool nativeActive=true;int nativeCloses=0;
    const auto unavailable=[&](LandCommandSource source) {
        if(s.releaseUnavailableSource(source)){nativeActive=false;++nativeCloses;}
    };
    unavailable(LandCommandSource::Missing);
    assert(nativeActive&&nativeCloses==0&&!s.frame().available&&!s.suppressStock(100));
    unavailable(LandCommandSource::Missing);assert(nativeActive&&nativeCloses==0);
    // The helper appears: native stock ownership can now be represented, but
    // disappearance must release only Android authority, not close that tree.
    s.update("scene",frame(),200);assert(s.offer(action(s,"ack","1"),201));assert(s.covered(201));
    auto oldAck=action(s,"ack","1"),oldAction=action(s,"activateItem","i0");
    assert(s.offer(oldAction,202));unavailable(LandCommandSource::Missing);
    assert(nativeActive&&nativeCloses==0&&!s.covered(203)&&!s.suppressStock(203));
    assert(!s.take(203)&&!s.offer(oldAck,203)&&!s.offer(oldAction,203));
    // A missing helper during a pending Android operation also restores stock;
    // even reaching the former grace deadline cannot close the foreign owner.
    s.update("scene",frame(),300);assert(s.offer(action(s,"selectItem","i0"),301));
    auto select=s.take(301);assert(select);s.finishAndroidOperation(*select,true,301);s.update("scene",frame(),302);
    assert(s.holdingPresentation());oldAck=action(s,"ack","1");oldAction=action(s,"activateItem","i0");
    unavailable(LandCommandSource::Missing);
    assert(nativeActive&&nativeCloses==0&&!s.suppressStock(303)&&!s.expireHandoff(1301));
    assert(!s.offer(oldAck,1301)&&!s.offer(oldAction,1301));
    // Recovery gives a fresh owner. No disappeared session's tokens revive.
    s.update("scene",frame(),1400);assert(!s.offer(oldAck,1400)&&!s.offer(oldAction,1400));
    assert(s.offer(action(s,"ack","1"),1401));assert(s.covered(1401));
    // Explicit original gameplay prohibition is deliberately different from
    // absence of presentation support: this result requests native Close.
    oldAck=action(s,"ack","1");unavailable(LandCommandSource::Forbidden);
    assert(!nativeActive&&nativeCloses==1&&!s.suppressStock(1402)&&!s.offer(oldAck,1402));
    // A still-present, compatible source retains timeout-to-Close behavior.
    nativeActive=true;s.update("scene",frame(),1500);assert(s.offer(action(s,"selectItem","i0"),1501));
    select=s.take(1501);s.finishAndroidOperation(*select,true,1501);s.update("scene",frame(),1502);
    assert(!s.releaseUnavailableSource(LandCommandSource::Ready));assert(s.holdingPresentation());
    if(s.expireHandoff(2501)){nativeActive=false;++nativeCloses;s.update("scene",{},2501);}
    assert(!nativeActive&&nativeCloses==2&&!s.frame().active);
    std::cout<<"PASS: missing adapter before takeover/after drawn ownership/during handoff preserves native stock; queues, ACKs and holds revoked; recovery fenced; explicit source prohibition and known timeout still request Close\n";
}
