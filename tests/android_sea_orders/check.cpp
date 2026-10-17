#include <platform/android_sea_orders.hpp>
#include <cassert>
#include <iostream>
using namespace storm::android;
std::string request(const SeaOrdersState &s, const std::string &verb, const std::string &id = "")
{
    return "1\t" + std::to_string(s.sceneEpoch()) + "\t" + std::to_string(s.revision()) + "\t" +
        std::to_string(s.activityGeneration()) + "\t" + verb + "\t" + id;
}
SeaOrdersFrame fixture()
{
    SeaOrdersFrame f;
    f.available = true;
    f.recipients.push_back({"r11_0", "Flagship", "11", 0, true});
    f.recipients.push_back({"r12_3", "Companion", "12", 3, false});
    f.recipientId = "r11_0";
    return f;
}
int main()
{
    SeaOrdersState s;
    s.resetActivity();
    auto f = fixture();
    s.update("battle1:ship11", f);
    assert(s.offer(request(s,"open"), 100));
    assert(s.take(101)->verb == "open");
    const auto oldScene = s.sceneEpoch();
    s.invalidate();
    f.active = true; f.mode = 32;
    f.items.push_back({"i0", "command", "Drift", "native-drift", true, 1});
    f.items.push_back({"i1", "command", "Ability", "native-ability", false, .25});
    s.update("battle1:ship11", f);
    assert(s.sceneEpoch() == oldScene); // ComState transition is not scene exit.
    assert(!s.offer(request(s,"open"), 100));
    assert(!s.offer(request(s,"activateItem","i1"), 100));
    assert(!s.offer(request(s,"activateItem","i999"), 100));
    assert(!s.offer(request(s,"selectRecipient","r12_99"), 100));
    assert(!s.offer(request(s,"back","i0"), 100));
    assert(!s.offer(request(s,"executeScript","BI_Attack"), 100));
    assert(!s.offer("1\tbad\t1\t1\tclose\t", 100));
    assert(!s.offer(request(s,"close") + "\textra", 100));
    assert(!s.offer(std::string(257,'x'), 100));
    assert(s.offer(request(s,"ack","1"), 100)); assert(s.covered());
    assert(s.offer(request(s,"ack","0"), 100)); assert(!s.covered());
    const auto beforeCooldown = s.revision();
    f.items[1].cooldown = .55; s.update("battle1:ship11", f);
    assert(s.revision() == beforeCooldown);
    const auto stale = request(s,"activateItem","i0");
    assert(s.offer(stale, 100));
    f.items[0].identity = "different-command-same-position-and-label";
    s.update("battle1:ship11", f);
    assert(!s.take(101)); assert(!s.offer(stale, 101));
    const auto beforeReady = s.revision();
    f.items[1].enabled = true; f.items[1].cooldown = 1; s.update("battle1:ship11", f);
    assert(s.revision() != beforeReady);
    assert(s.offer(request(s,"activateItem","i0"),100));
    assert(s.offer(request(s,"activateItem","i0"),100));
    assert(s.take(101)); s.invalidate(); assert(!s.take(101)); // one effect per revision
    for (int i=0;i<8;++i) assert(s.offer(request(s,"selectItem","i0"),100));
    assert(!s.offer(request(s,"selectItem","i0"),100));
    assert(!s.take(1601)); // all stale, no actions remain
    assert(s.offer(request(s,"close"),0xfffffff0u)); assert(s.take(0x10u));
    const auto oldActivity = request(s,"close"); s.resetActivity();
    assert(!s.offer(oldActivity, 100)); assert(!s.covered());
    assert(s.offer(request(s,"ack","1"),100)); assert(s.covered());
    f.complete = false; s.update("battle1:ship11", f);
    assert(s.offer(request(s,"ack","1"),100)); assert(!s.covered());
    assert(s.json().find("\"complete\":false") != std::string::npos);
    const auto old = request(s,"close");
    s.update("battle2:ship11", f); assert(s.sceneEpoch() != oldScene); assert(!s.offer(old,101));
    s.update({},{}); assert(!s.offer(request(s,"open"),100)); assert(!s.covered());
    f = fixture(); f.active = true; f.recipients[0].name = "";
    f.items.push_back({"i0", "command", "   ", "native-missing-note", true, 1});
    s.update("missing-labels", f);
    assert(!s.frame().complete && s.frame().active && s.frame().available);
    assert(s.frame().recipients[0].name == "\xE2\x80\xA6");
    assert(s.frame().items[0].label == "\xE2\x80\xA6");
    assert(s.offer(request(s,"back"),100)); assert(s.take(101)->verb == "back");
    assert(s.offer(request(s,"close"),100)); assert(s.take(101)->verb == "close");
    f.recipients[0].name = "\xC2\x85";
    s.update("c1-control-label", f);
    assert(!s.frame().complete && s.frame().recipients[0].name == "\xE2\x80\xA6");
    assert(s.offer(request(s,"close"),100)); assert(s.take(101)->verb == "close");
    f = fixture(); f.active = true; f.selectedItemId = "i0";
    for (int i=0;i<256;++i)
        f.items.push_back({"i"+std::to_string(i), "command", std::string(93, '\"'), "native-quoted-"+std::to_string(i), true, 1});
    s.update("escaped-size", f);
    assert(s.json().size() <= 65536 && !s.frame().complete && s.frame().active && s.frame().available);
    assert(s.offer(request(s,"back"),100)); assert(s.take(101)->verb == "back");
    assert(s.offer(request(s,"close"),100)); assert(s.take(101)->verb == "close");
    std::cout << "PASS: sea order scene/activity guards, dynamic identities, cooldown, queue, ACK and duplicate rejection\n";
}
