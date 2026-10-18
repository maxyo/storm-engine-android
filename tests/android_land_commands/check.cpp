#include <platform/android_land_commands.hpp>
#include <cassert>
#include <iostream>
using namespace storm::android;
std::string request(const LandCommandsState &s,const std::string &verb,const std::string &id="") {
    return "1\t"+std::to_string(s.sceneEpoch())+"\t"+std::to_string(s.revision())+"\t"+std::to_string(s.activityGeneration())+"\t"+verb+"\t"+id;
}
LandCommandsFrame fixture() {
    LandCommandsFrame f;f.available=true;f.active=true;f.selectedItemId="i0";
    // Identical native captions must keep different source/target identities.
    f.items={{"i0","command","Открыть","BI_OutDoor","door:reload1",true,1},
        {"i1","command","Открыть","BI_UseBox","box:treasure",true,1},
        {"i2","cancel","Отмена","","cancel",true,1}};
    return f;
}
int main() {
    LandCommandsState s;s.resetActivity();auto f=fixture();s.update("bli:1:player:2:scene:3:target:4",f);
    assert(s.frame().items[0].source!=s.frame().items[1].source);
    assert(!s.offer(request(s,"activateItem","i1"),100)); // selection acknowledgement required
    assert(s.offer(request(s,"selectItem","i1"),100));
    assert(!s.offer(request(s,"activateItem","i0"),100)); // no overlapping actions
    assert(s.take(101)->id=="i1");s.invalidate();f.selectedItemId="i1";s.update("bli:1:player:2:scene:3:target:4",f);
    assert(s.offer(request(s,"activateItem","i1"),100));assert(s.take(101));s.invalidate();assert(!s.take(101));
    assert(!s.offer(request(s,"executeScript","BI_UseBox"),100));
    assert(!s.offer(request(s,"selectRecipient","i1"),100));
    assert(!s.offer(request(s,"open","r0"),100));
    assert(!s.offer(request(s,"back","i1"),100));
    assert(!s.offer(request(s,"close")+"\textra",100));
    assert(s.offer(request(s,"ack","1"),100));assert(s.covered(850));assert(!s.covered(851));
    assert(s.offer(request(s,"ack","1"),600));assert(s.covered(1000));
    const auto stale=request(s,"activateItem","i1");assert(s.offer(stale,600));
    f.items[1].identity="box:different-chest-same-caption";s.update("bli:1:player:2:scene:3:target:4",f);
    assert(!s.take(601)&&!s.offer(stale,601)&&!s.covered(601));
    for(auto owner:{"bli:9:player:2:scene:3:target:4","bli:9:player:7:scene:3:target:4","bli:9:player:7:scene:8:target:4","bli:9:player:7:scene:8:target:6"}) {
        auto old=request(s,"activateItem","i1");s.update(owner,f);assert(!s.offer(old,602));
    }
    f.items[1].enabled=false;f.items[1].cooldown=.25;s.update("cooldown",f);assert(!s.offer(request(s,"activateItem","i1"),100));
    auto revision=s.revision();f.items[1].cooldown=.5;s.update("cooldown",f);assert(revision==s.revision());
    f.items[1].enabled=true;f.items[1].cooldown=1;s.update("cooldown",f);assert(revision!=s.revision());
    auto removed=request(s,"activateItem","i1");f.items.erase(f.items.begin()+1);f.selectedItemId="i0";s.update("cooldown",f);assert(!s.offer(removed,100));
    assert(s.offer(request(s,"activateItem","i0"),100));assert(s.offer(request(s,"ack","0"),100));assert(!s.take(101));
    auto beforeActivity=request(s,"close");s.resetActivity();assert(!s.offer(beforeActivity,100));
    assert(s.offer(request(s,"ack","1"),100));assert(s.covered(101));s.resetActivity();assert(!s.covered(101));
    f.complete=false;s.update("incomplete",f);assert(s.offer(request(s,"ack","1"),100));assert(!s.covered(101));
    assert(!s.offer(request(s,"activateItem","i0"),100));assert(s.offer(request(s,"close"),100));assert(s.take(101));
    f=fixture();f.items[0].label="";s.update("missing",f);assert(!s.frame().complete&&s.frame().available);
    f=fixture();f.items[0].source=std::string(513,'a');s.update("invalid-source",f);assert(!s.frame().complete);
    f=fixture();f.items[0].label="\xc2\x85";s.update("invalid-utf8",f);assert(!s.frame().complete);
    f=fixture();for(int i=3;i<257;++i)f.items.push_back({"i"+std::to_string(i),"travel","Route","actual-source","location:locator",true,1});
    s.update("oversize",f);assert(!s.frame().complete&&s.frame().items.empty()&&s.json().size()<=65536);
    f=fixture();f.active=false;f.items.clear();f.selectedItemId.clear();s.update("closed",f);
    assert(s.offer(request(s,"open"),100));assert(!s.take(1601));
    assert(s.offer(request(s,"open"),0xfffffff0u));assert(s.take(0x10u));
    s.update({},{});assert(!s.offer(request(s,"open"),100));
    std::cout<<"PASS: land selection acknowledgement, duplicate-caption identity, stale target/scene/activity, removed/disabled entries, cooldown, completeness, bounds, lease expiry, queue age and duplicate rejection\n";
}
