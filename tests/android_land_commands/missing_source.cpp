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
    assert(nativeActive&&nativeCloses==0&&!s.frame().available);
    unavailable(LandCommandSource::Missing);assert(nativeActive&&nativeCloses==0);
    // The helper appears; its disappearance must release only Android authority (queued and
    // in-flight taps), not close the native tree.
    s.update("scene",frame());
    auto oldAction=action(s,"activateItem","i0");
    assert(s.offer(oldAction,202));unavailable(LandCommandSource::Missing);
    assert(nativeActive&&nativeCloses==0);
    assert(!s.take(203)&&!s.offer(oldAction,203));
    // A missing helper after an executed Android operation: nothing of it revives.
    s.update("scene",frame());assert(s.offer(action(s,"selectItem","i0"),301));
    auto select=s.take(301);assert(select);s.invalidate();s.update("scene",frame());
    oldAction=action(s,"activateItem","i0");
    unavailable(LandCommandSource::Missing);
    assert(nativeActive&&nativeCloses==0&&!s.offer(oldAction,1301));
    // Recovery gives a fresh owner. No disappeared session's tokens revive.
    s.update("scene",frame());assert(!s.offer(oldAction,1400));
    // Explicit original gameplay prohibition is deliberately different from
    // absence of presentation support: this result requests native Close.
    oldAction=action(s,"activateItem","i0");unavailable(LandCommandSource::Forbidden);
    assert(!nativeActive&&nativeCloses==1&&!s.offer(oldAction,1402));
    nativeActive=true;s.update("scene",frame());
    assert(!s.releaseUnavailableSource(LandCommandSource::Ready)&&nativeCloses==1);
    std::cout<<"PASS: a missing adapter preserves native stock; queued and in-flight taps revoked; recovery fenced; explicit source prohibition requests Close\n";
}
