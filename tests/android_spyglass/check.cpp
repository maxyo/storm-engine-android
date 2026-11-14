#include "platform/android_spyglass.hpp"
#include <cassert>
#include <string>

int main()
{
    using namespace storm::android;
    assert(spyglassCaptainLine("24 фунт.       Мигель де Сандоваль") == std::make_pair(std::string("24 фунт."), std::string("Мигель де Сандоваль")));
    assert(spyglassCaptainLine("Кулеврины  Мушкетный залп   Ян ван Дейк").second == "Ян ван Дейк");
    assert(spyglassCaptainLine("       Хуан").first.empty() && spyglassCaptainLine("       Хуан").second == "Хуан");
    assert(spyglassCaptainLine("   ").second.empty());

    SpyglassState s;
    assert(s.json() == "{\"schemaVersion\":1,\"revision\":0,\"on\":false,\"generation\":0,\"target\":null}");
    assert(!s.acknowledge("0\t1")); // nothing to cover while the telescope is down
    s.turn(true);
    assert(s.on() && s.generation() == 1 && s.json().find("\"target\":null") != std::string::npos);
    SpyglassTarget t;
    t.name = "Санта \"Каталина\""; t.type = "Галеон"; t.shipClass = 2; t.hull = 64; t.sails = 80; t.cannons = 32; t.cannonsMax = 36;
    t.charge = 1; t.nation = 2; t.speed = 11.04; t.distance = 420.0; t.captain = "Мигель"; t.skills[0] = 7; t.skills[1] = 8;
    s.target(t);
    const std::string json = s.json();
    assert(json.find("\"kind\":\"ship\"") != std::string::npos);
    assert(json.find("\"name\":\"Санта \\\"Каталина\\\"\"") != std::string::npos);
    assert(json.find("\"speed\":11.0") != std::string::npos && json.find("\"crew\"") == std::string::npos); // hidden: left out
    assert(json.find("\"skills\":{\"fencing\":7,\"cannons\":8}") != std::string::npos);
    const std::string before = s.json();
    s.target(t);
    assert(s.json() == before); // the same target is no new revision

    // Coverage: the UI's word for the current raise, kept for the raise (no lease), gone when the
    // telescope drops.
    assert(!s.acknowledge("0\t1") && !s.acknowledge("x\t1") && !s.acknowledge("1") && !s.acknowledge("1\t2") && !s.covered());
    assert(s.acknowledge("1\t1") && s.covered() && !s.awaiting());
    assert(s.acknowledge("1\t0") && !s.covered()); // the overlay went away: native info back
    s.turn(false);
    assert(!s.covered() && s.json().find("\"on\":false") != std::string::npos);
    s.turn(true);
    assert(s.generation() == 3 && !s.covered() && s.awaiting() && !s.acknowledge("1\t1"));
    // A presenting UI: an unanswered raise keeps the native info off; a silent UI is withdrawn
    // loudly after HudPresenters::kSilentMs of frame time and the native info comes back.
    assert(hudPresenters.declare("spyglass") && s.covered());
    uint32_t tick = 500;
    for (uint32_t i = 0; i < HudPresenters::kSilentMs / 50; ++i, tick += 50) assert(!hudPresenters.watch("spyglass", s.awaiting(), tick));
    const auto line = hudPresenters.watch("spyglass", s.awaiting(), tick);
    assert(line && line->find("HUD presenter spyglass withdrawn") != std::string::npos && !s.covered());
    assert(s.acknowledge("3\t1") && s.covered()); // a late answer still covers
    assert(hudPresenters.watch("spyglass", s.awaiting(), tick + 16) && hudPresenters.spyglass); // and brings the presenter back
    s.clear();
    t.fort = true;
    s.target(t);
    assert(s.json().find("\"kind\":\"fort\"") != std::string::npos);
    return 0;
}
