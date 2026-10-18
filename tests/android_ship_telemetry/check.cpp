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
    assert(shipCaptionText("  Сан-Хуан  ") == "Сан-Хуан");
    assert(shipCaptionText(std::string(160, 'a')).size() == 160);
    assert(shipCaptionText(std::string(161, 'a')).empty());
    assert(shipCaptionText(std::string(159, 'a') + "Я").empty());
    assert(shipCaptionText(std::string(98, 'a') + "\xC0\xAF").empty()); // validate beyond old name limit
    for (const auto invalid : {"\xDF", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "bad\ntext"})
        assert(shipCaptionText(invalid).empty());
    assert(shipCaptionText(" ").empty());
    assert(shipDateTimeText("Дата: 17 октября 1665 12:34", "Дата:") == "17 октября 1665 12:34");
    assert(shipDateTimeText("Date: 17 October 1665 12:34", "Date:") == "17 October 1665 12:34");
    assert(shipDateTimeText("Datum: 17 Oktober 1665 12:34", "Datum: ") == "17 Oktober 1665 12:34");
    assert(shipDateTimeText("Дата: 12:34", "Date:").empty());
    assert(shipDateTimeText("unknown 12:34", "").empty());
    assert(shipDateTimeText("12:34 Date:", "Date:").empty());
    assert(shipDateTimeText("Date:" + std::string(161, 'a'), "Date:").empty());
    assert(shipDisplayedSpeed(-1.26) == -1.3);
    assert(shipDisplayedSpeed(-3.15f * 0.4f) == -1.3); // native float scaling, signed arcade speed
    assert(shipDisplayedSpeed(3.15f * 1.0f) == 3.2);
    assert(shipDisplayedSpeed(1.24) == 1.2);
    assert(shipDisplayedSpeed(0) == 0);
    assert(!shipDisplayedSpeed(std::numeric_limits<double>::infinity()));
    assert(!shipDisplayedSpeed(std::numeric_limits<double>::quiet_NaN()));
    assert(shipPowderCount("0") == 0);
    assert(shipPowderCount("125") == 125);
    for (const auto invalid : {"", "-1", "2.5", "NaN", "unknown", "2147483648"}) assert(!shipPowderCount(invalid));
    assert(!shipHideSpeedHeading(ShipSpeed));
    assert(!shipHideSpeedHeading(ShipWind));
    assert(shipHideSpeedHeading(ShipSpeed | ShipWind));
    assert(!(shipCaptionMask(false, false, ShipAll) & ShipCaptions));
    assert(!(shipCaptionMask(true, true, ShipAll) & ShipCaptions));
    assert(shipCaptionMask(true, false, ShipAll) == ShipAll);
    assert(shipAmmoCount("0") == 0);
    assert(shipAmmoCount("125") == 125);
    assert(shipAmmoCount("2147483648") == 2147483648LL);
    for (const auto invalid : {"", "-1", "2.5", "NaN", "Inf", "unknown", "9223372036854775808"}) assert(!shipAmmoCount(invalid));
    // Inventory is independent from the selected charge and preserves absent vs zero.
    ShipTelemetryState inventoryState;
    ShipTelemetryFrame inventory;
    inventory.valid = true;
    inventory.ship.entityId = "cargo-owner";
    inventory.ammo = "balls";
    inventory.ammoType = 0;
    inventory.ammoCount = 7;
    inventory.ammoCounts = {0, 25, 36, 47};
    inventory.ship.type = "Lugger";
    inventory.ship.shipClass = 5;
    inventoryState.update(10, inventory);
    const auto inventoryFirst = inventoryState.publication(0);
    assert(inventoryFirst);
    assert(inventoryFirst->find("\"ammoCount\":7") != std::string::npos);
    assert(inventoryFirst->find("\"type\":\"Lugger\"") != std::string::npos);
    assert(inventoryFirst->find("\"shipClass\":5") != std::string::npos);
    assert(inventoryFirst->find("\"ammoCounts\":{\"balls\":0,\"grapes\":25,\"knippels\":36,\"bombs\":47}") != std::string::npos);
    inventoryState.published(*inventoryFirst, 0);
    const auto inventoryGeneration = inventoryState.hudRevision();
    inventory.ammoCounts[1] = 24;
    inventoryState.update(10, inventory);
    assert(inventoryState.hudRevision() == inventoryGeneration && inventoryState.revision() == 10);
    assert(!inventoryState.publication(99));
    const auto inventoryChanged = inventoryState.publication(100);
    assert(inventoryChanged && inventoryChanged->find("\"grapes\":24") != std::string::npos);
    assert(inventoryChanged->find("\"balls\":0") != std::string::npos);
    inventory.ammoCounts[2].reset();
    inventoryState.update(10, inventory);
    assert(inventoryState.json().find("\"knippels\":") == std::string::npos);
    inventory.ammoCounts = {};
    inventoryState.update(10, inventory);
    assert(inventoryState.json().find("\"ammoCounts\":{}") != std::string::npos);
    assert(inventoryState.json().find("\"ammoCount\":7") != std::string::npos);
    inventoryState.update(11, {});
    const auto inventoryCleared = inventoryState.publication(101);
    assert(inventoryCleared && inventoryCleared->find("\"valid\":false") != std::string::npos);
    assert(inventoryCleared->find("\"ammoCounts\":") == std::string::npos);

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
    assert(!state.acknowledge(ack(2048)));
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
    // Caption replacements require their own current render ACK. Content changes
    // are throttled without invalidating held controls or the action epoch.
    ShipTelemetryState captions;
    frame.captionsVisible = true;
    frame.shipSpeedKnots = -1.3;
    frame.windSpeed = 5.4;
    frame.powderCount = 0;
    frame.dateTimeText = "17 октября 1665 12:34";
    frame.locationText = "Сан-Хуан";
    captions.update(epoch, frame);
    const auto captionAck = [&captions, &request](int mask) {
        return request(mask) + '\t' + std::to_string(captions.hudRevision());
    };
    assert((captions.frame().availableMask() & ShipCaptions) == ShipCaptions);
    assert(captions.mask() == 0);
    assert(captions.acknowledge(captionAck(ShipSpeed)));
    assert(captions.mask() == ShipSpeed && !shipHideSpeedHeading(captions.mask()));
    assert(captions.acknowledge(captionAck(ShipCaptions)));
    assert(captions.mask() == ShipCaptions && shipHideSpeedHeading(captions.mask()));
    const auto allCaptions = captions.publication(0);
    assert(allCaptions && allCaptions->find("\"captionsVisible\":true") != std::string::npos);
    assert(allCaptions->find("\"shipSpeedKnots\":-1.3") != std::string::npos);
    assert(allCaptions->find("\"windSpeed\":5.4") != std::string::npos);
    assert(allCaptions->find("\"powderCount\":0") != std::string::npos);
    assert(allCaptions->find("17 октября 1665 12:34") != std::string::npos);
    assert(allCaptions->find("Сан-Хуан") != std::string::npos);
    captions.published(*allCaptions, 0);
    const auto captionRevision = captions.hudRevision();
    frame.shipSpeedKnots = -1.2;
    frame.windSpeed = 5.5;
    frame.powderCount = 1;
    frame.dateTimeText = "17 октября 1665 12:35";
    frame.locationText = "Порт Сан-Хуан";
    captions.update(epoch, frame);
    assert(captions.hudRevision() == captionRevision && captions.revision() == epoch);
    assert(captions.mask() == ShipCaptions);
    assert(!captions.publication(99) && captions.publication(100));
    const auto staleCaptionAck = captionAck(ShipCaptions);
    frame.captionsVisible = false;
    captions.update(epoch, frame);
    assert(captions.hudRevision() > captionRevision && !(captions.mask() & ShipCaptions));
    assert(captions.publication(1)); // hide bypasses throttle
    assert(!captions.acknowledge(staleCaptionAck));
    frame.captionsVisible = true;
    captions.update(epoch, frame);
    assert(!(captions.mask() & ShipCaptions));
    assert(!captions.acknowledge(staleCaptionAck));
    assert(captions.acknowledge(captionAck(ShipCaptions)));
    for (const auto bit : {ShipSpeed, ShipWind, ShipPowder, ShipDateTime, ShipLocation})
    {
        auto missing = frame;
        if (bit == ShipSpeed) missing.shipSpeedKnots.reset();
        if (bit == ShipWind) missing.windSpeed.reset();
        if (bit == ShipPowder) missing.powderCount.reset();
        if (bit == ShipDateTime) missing.dateTimeText.clear();
        if (bit == ShipLocation) missing.locationText.clear();
        const auto stale = captionAck(ShipCaptions);
        captions.update(epoch, missing);
        assert(!(captions.mask() & bit));
        assert(!captions.acknowledge(stale));
        assert(captions.acknowledge(captionAck(ShipCaptions)));
        assert(!(captions.mask() & bit));
        captions.update(epoch, frame);
        assert(!(captions.mask() & bit));
        assert(captions.acknowledge(captionAck(ShipCaptions)));
        assert(captions.mask() == ShipCaptions);
    }
    captions.resetActivity();
    assert(captions.mask() == 0);
    captions.update(epoch, {});
    assert(!captions.acknowledge(captionAck(ShipCaptions)));

}
