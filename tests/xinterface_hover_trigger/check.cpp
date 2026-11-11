#include "hover_trigger.hpp"

#include <cassert>
#include <iostream>

using storm::xinterface::HoverTrigger;
using Event = HoverTrigger::Event;

int main()
{
    int asked = 0;
    const auto delay = [&](float value) {
        return [&asked, value] {
            asked++;
            return value;
        };
    };

    // Rest on A: wait the delay, start, fill for 1 s, end; leaving cancels.
    HoverTrigger t;
    assert(t.update("", 0.1f, 1.f, delay(0.5f)).empty() && asked == 0);
    assert(t.update("A", 0.1f, 1.f, delay(0.5f)).empty() && asked == 1); // hover begins, delay asked once
    assert(t.progress(1.f) < 0.f);
    assert(t.update("A", 0.4f, 1.f, delay(0.5f)).empty());
    auto e = t.update("A", 0.2f, 1.f, delay(0.5f)); // 0.6 s: started, 0.1 s already filled
    assert(e.size() == 1 && e[0].event == Event::Start && e[0].node == "A" && asked == 1);
    assert(t.progress(1.f) > 0.09f && t.progress(1.f) < 0.11f);
    e = t.update("A", 0.95f, 1.f, delay(0.5f));
    assert(e.size() == 1 && e[0].event == Event::End && e[0].node == "A");
    assert(t.progress(1.f) < 0.f);
    assert(t.update("A", 5.f, 1.f, delay(0.5f)).empty()); // stays confirmed
    e = t.update("", 0.1f, 1.f, delay(0.5f));
    assert(e.size() == 1 && e[0].event == Event::Cancel);

    // Moving from a filling node to another: cancel, then a new wait; zero delay starts at once.
    t.update("A", 0.f, 1.f, delay(0.f));
    e = t.update("A", 0.f, 1.f, delay(0.f));
    assert(t.progress(1.f) >= 0.f);
    e = t.update("B", 0.1f, 1.f, delay(0.f));
    assert(e.size() == 2 && e[0].event == Event::Cancel && e[1].event == Event::Start && e[1].node == "B");

    // Leaving before the start emits nothing; a negative delay disables this hover.
    HoverTrigger quiet;
    quiet.update("A", 0.f, 1.f, delay(2.f));
    assert(quiet.update("", 0.5f, 1.f, delay(2.f)).empty());
    quiet.update("C", 0.f, 1.f, delay(-1.f));
    assert(quiet.update("C", 10.f, 1.f, delay(-1.f)).empty() && quiet.progress(1.f) < 0.f);

    // Switching the TRIGGER node off cancels a started node only.
    HoverTrigger off;
    off.update("A", 0.f, 1.f, delay(0.f));
    e = off.reset();
    assert(e.size() == 1 && e[0].event == Event::Cancel);
    assert(off.reset().empty());

    std::cout << "hover trigger: delay, start, fill, end, cancel, disable PASS\n";
}
