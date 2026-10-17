#include "platform/android_ship_telemetry.hpp"
#include <cassert>
#include <limits>

int main()
{
    using namespace storm::android;
    assert(shipTextValue("Ласточка") == "Ласточка");
    assert(shipTextValue(std::string(95, 'a') + "Я") == std::string(95, 'a'));
    for (const auto invalid : {"\xDF", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80"}) assert(shipTextValue(invalid).empty());
    for (const auto invalid : {"", "unknown", "0junk", "NaN", "Inf", "-1", "1,5"}) assert(!shipNumberValue(invalid));
    assert(shipNumberValue("0") == 0);
    assert(shipNumberValue(" 1.5e2 ") == 150);
    assert(!shipMetric(0, 0));
    assert(!shipMetric(-1, 100));
    assert(!shipMetric(std::numeric_limits<double>::quiet_NaN(), 100));
    assert(!shipMetric(50, std::numeric_limits<double>::infinity()));
    assert(shipMetric(120, 100)->current == 120); // raw values are not fabricated/clamped
    ShipSailGuard guard{true, true, true, true, false, false, true, 0.5};
    assert(shipSailControlAllowed(guard));
    for (auto flag : {&ShipSailGuard::sea, &ShipSailGuard::up, &ShipSailGuard::down,
                      &ShipSailGuard::mainCharacter, &ShipSailGuard::canDropSails})
    {
        auto blocked = guard;
        blocked.*flag = false;
        assert(!shipSailControlAllowed(blocked));
    }
    guard.stopped = true;
    assert(!shipSailControlAllowed(guard));
    guard.stopped = false;
    guard.dead = true;
    assert(!shipSailControlAllowed(guard));
    guard.dead = false;
    for (double value : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        guard.state = value;
        assert(!shipSailControlAllowed(guard));
    }
    guard.state.reset();
    assert(!shipSailControlAllowed(guard));

    GameplayState gameplay;
    gameplay.update({"sailing", "russian", "Sailing3Pers", "ship-1", {"Ship_SailUp", "Ship_SailDown"}});
    const auto epoch = gameplay.revision();
    ShipTelemetryState state;
    ShipTelemetryFrame frame;
    frame.valid = true;
    frame.ship = {1, "123456789", "Ship", "Lugger", shipMetric(50, 100), shipMetric(30, 40), shipMetric(80, 100)};
    frame.sailState = 0.5;
    frame.canSetSails = true;
    frame.ammo = "balls";
    frame.ammoType = 0;
    frame.ammoCount = 0; // zero inventory is valid, distinct from unknown
    frame.fleet.push_back({2, "123456790", "Friend", "Sloop", shipMetric(10, 100), shipMetric(20, 60), shipMetric(70, 100)});
    state.update(epoch, frame);
    const auto request = [epoch](int value) { return std::to_string(epoch) + '\t' + std::to_string(value); };
    const auto ack = [&state, &request](int mask) { return request(mask) + '\t' + std::to_string(state.hudRevision()); };
    assert(!state.acknowledge(request(63))); // legacy unversioned coverage ACK must fail closed
    assert(state.mask() == 0); // telemetry delivery alone never hides native UI
    assert(state.acknowledge(ack(63)));
    assert(state.mask() == 63);
    assert(!state.acknowledge(ack(64)));
    assert(!state.acknowledge("0\t63"));
    assert(!state.acknowledge(ack(1) + "junk"));
    const auto first = state.publication(0);
    assert(first && first->find("\"ammoCount\":0") != std::string::npos);
    assert(first->find("\"entityId\":\"123456789\"") != std::string::npos);
    state.published(*first, 0);
    assert(!state.publication(200));
    assert(state.offerSails(request(0), 10));
    assert(state.offerSails(request(2), 11));
    const auto stableHud = state.hudRevision();
    frame.ship.hull = shipMetric(49, 100);
    state.update(epoch, frame);
    assert(state.hudRevision() == stableHud);
    assert(gameplay.revision() == epoch); // HP updates cannot cancel held inputs/action epoch
    assert(state.takeSails(12) == 2); // latest slider value wins; HP changes do not cancel it
    assert(!state.takeSails(12));
    assert(!state.publication(99));
    assert(state.publication(100));
    frame.ammoCount.reset();
    frame.ship.crew.reset();
    frame.fleet.front().sails.reset();
    state.update(epoch, frame);
    assert(!(state.mask() & (ShipAmmo | ShipCrew | ShipFleet))); // missing fields restore originals immediately
    frame.ammoCount = 1;
    state.update(epoch, frame);
    assert(!(state.mask() & ShipAmmo)); // reappearing field needs a new rendered ACK
    assert(state.acknowledge(ack(63)));
    assert(state.mask() & ShipAmmo);
    frame.fleet.front().sails = shipMetric(70, 100);
    state.update(epoch, frame);
    assert(state.acknowledge(ack(63)));
    const auto staleFleetAck = ack(63);
    frame.fleet.front().entityId = "replacement-entity";
    state.update(epoch, frame);
    assert(!(state.mask() & ShipFleet)); // new identity must render before native card hides
    assert(!state.acknowledge(staleFleetAck)); // delayed old-render ACK cannot bypass structural revoke
    assert(!state.offerSails(request(3), 20));
    assert(!state.offerSails(request(1) + "x", 20));
    assert(state.offerSails(request(1), 20));
    frame.canSetSails = false;
    state.update(epoch, frame);
    assert(!state.takeSails(21)); // stopped/quest lock changed after touch
    frame.canSetSails = true;
    state.update(epoch, frame);
    assert(state.offerSails(request(1), 20));
    assert(!state.takeSails(1521));
    assert(state.offerSails(request(1), std::numeric_limits<uint32_t>::max() - 10));
    assert(state.takeSails(10) == 1);
    assert(state.offerSails(request(1), 40));
    const auto staleActivityAck = ack(63);
    state.resetActivity();
    assert(!state.acknowledge(staleActivityAck));
    assert(state.mask() == 0 && !state.takeSails(41));
    assert(state.publication(41)); // unchanged payload replays after recreation
    state.update(epoch + 1, frame);
    assert(!state.acknowledge(ack(63)) && !state.offerSails(request(1), 42));
    auto current = state.publication(42);
    assert(current);
    state.published(*current, 42);
    state.update(epoch + 1, {});
    const auto cleared = state.publication(43);
    assert(cleared && cleared->find("\"valid\":false") != std::string::npos); // clear bypasses throttle
    assert(state.mask() == 0);
}
