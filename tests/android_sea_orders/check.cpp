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
    SeaOrdersRefreshGate gate;
    unsigned collections = 0;
    // Matched one-second input: 1000 loop iterations, a closed orders menu,
    // unchanged live battle/ship owner. Count real collection decisions.
    for (uint32_t tick = 0; tick < 1000; ++tick)
        collections += gate.due(true, 10, 20, 3, false, tick);
    assert(collections == 10);
    assert(!gate.due(true, 10, 20, 3, false, 999));
    assert(gate.due(true, 11, 20, 3, false, 999)); // battle replacement
    assert(gate.due(true, 11, 21, 3, false, 999)); // flagship replacement
    assert(gate.due(true, 11, 21, 4, false, 999)); // character identity
    assert(gate.due(false, 11, 21, 4, false, 999)); // forbidden state closes immediately
    assert(!gate.due(false, 11, 21, 4, false, 2000)); // no repeated empty snapshots
    assert(gate.due(true, 11, 21, 4, false, 2000)); // ready again
    assert(gate.due(true, 11, 21, 4, true, 2001)); // native/script menu opened
    for (uint32_t tick = 2002; tick < 2100; ++tick)
        assert(gate.due(true, 11, 21, 4, true, tick)); // active predicates never delayed
    assert(gate.due(true, 11, 21, 4, false, 2100)); // menu closed
    assert(!gate.due(true, 11, 21, 4, false, 2101));
    gate.force();
    assert(gate.due(true, 11, 21, 4, false, 2101)); // native action or close
    gate.force();
    assert(gate.due(true, 11, 21, 4, false, 0xfffffff0u));
    assert(!gate.due(true, 11, 21, 4, false, 0x10u));
    assert(gate.due(true, 11, 21, 4, false, 0x54u)); // tick wrap, exactly 100 ms
    SeaOrdersState s;
    s.resetActivity();
    auto f = fixture();
    s.update("battle1:ship11", f);
    assert(s.offer(request(s,"open"), 100));
    assert(s.take(101)->verb == "open");
    // A recipient can be replaced between the periodic snapshot and a tap.
    // The dispatch refresh must change the revision even when its visible name
    // and position did not change; the old request cannot be offered again.
    const auto staleRecipient = request(s, "open", "r12_3");
    assert(s.offer(staleRecipient, 102));
    const auto pendingRecipient = s.take(102);
    const auto recipientRevision = s.revision();
    f.recipients[1].entityId = "99";
    f.recipients[1].id = "r99_3";
    s.update("battle1:ship11", f);
    assert(s.revision() != recipientRevision);
    assert(pendingRecipient->revision != s.revision());
    assert(!s.offer(staleRecipient, 103));
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
    std::cout << "PASS: sea order guards, live identities, cooldown, queue, ACK, duplicate rejection; closed-menu collections 10/1000 iterations in 1000 ms, active-menu refresh every iteration\n";
}
