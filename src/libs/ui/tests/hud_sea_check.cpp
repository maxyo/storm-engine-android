// Host checks for the sea HUD. First the consumers of its channels (ship, seaOrders,
// seaQuickAction) and the scene alone: what each synthetic state shows, which engine input
// each event sends, what is acknowledged as drawn. Then the whole runtime with the small
// synthetic pack: which touches the UI claims at sea and what real gestures send.
#include "hud_bench.h"

static bool Sent(const FakeMount &mount, const std::string &command)
{
    for (const std::string &sent : mount.commands)
        if (sent == command)
            return true;
    return false;
}

static size_t Count(const FakeMount &mount, const std::string &command)
{
    size_t count = 0;
    for (const std::string &sent : mount.commands)
        count += sent == command ? 1 : 0;
    return count;
}

static void Sail(Bench &bench)
{
    bench.Feed("seaOrders", "sea-orders-idle");
    bench.Feed("gameplay", "gameplay-sailing");
    bench.Feed("ship", "ship");
    bench.Feed("seaQuickAction", "sea-quick-moor");
}

static void CheckConsumers()
{
    Bench bench;
    const States &states = bench.hud.Current();
    FakeMount &mount = bench.mount;

    bench.Feed("ship", "ship");
    const Ship &ship = states.ship;
    Expect(ship.known && ship.gameplay_revision == 9 && ship.hud_revision == 3 && ship.own.name == "Аделина" && ship.fleet.size() == 3, "ship: the frame is read");
    Expect(std::abs(ship.own.hull - 0.82) < 1e-6 && std::abs(ship.fleet[2].hull - 0.41) < 1e-6 && ship.fleet[1].character == 8, "ship: bars are shares of their maximum");
    Expect(ship.ammo == "balls" && ship.ammo_count == 88 && ship.ammo_counts[3] == 16 && !ship.ammo_low && ship.sail_state == 1.0 && ship.can_set_sails,
           "ship: ammunition and sails");
    Expect(ship.captions && ship.navigator && ship.navigator_generation == 4 && ship.speed == 4.2 && ship.wind == 9.0 && ship.cannon_charge == 1.0 &&
               ship.location == "Мартиника",
           "ship: the readouts");
    Json late = State("ship");
    late["sequence"] = 19;
    late["locationText"] = "Гваделупа";
    bench.Set("ship", late);
    Expect(states.ship.location == "Мартиника" && mount.errors.empty(), "ship: an older frame of the same gameplay is not news");
    Json partial = State("ship");
    partial["sequence"] = 21;
    for (const char *key : {"hull", "crewMax", "shipSpeedKnots", "ammo", "sailState", "cannonCharge"})
        partial.erase(key);
    bench.Set("ship", partial);
    Expect(states.ship.known && states.ship.own.hull < 0 && states.ship.own.crew < 0 && states.ship.speed < 0 && states.ship.ammo.empty() &&
               states.ship.sail_state < 0 && states.ship.cannon_charge < 0 && mount.errors.empty(),
           "ship: what the engine does not know stays unknown");
    bench.Feed("ship", "ship-none");
    Expect(!states.ship.known && states.ship.hud_revision == 4 && mount.errors.empty(), "ship: no sea scene is not an error");
    size_t errors = 0;
    const auto rejected = [&](const char *channel, Json state, const std::string &what) {
        bench.Set(channel, state);
        Expect(mount.errors.size() == ++errors, what + " is rejected aloud");
    };
    Json bad = State("ship");
    bad["schemaVersion"] = 2;
    rejected("ship", bad, "ship: another schema");
    Expect(!states.ship.known, "ship: a rejected frame leaves no ship");
    bad = State("ship");
    bad.erase("fleet");
    rejected("ship", bad, "ship: a frame without the fleet list");
    bad = State("ship");
    bad["ammo"] = "harpoons";
    rejected("ship", bad, "ship: an unknown ammunition kind");
    bad = State("ship");
    bad["fleet"][0].erase("entityId");
    rejected("ship", bad, "ship: a companion without identity");
    rejected("ship", Json("sea"), "ship: a string");

    bench.Feed("seaOrders", "sea-orders-root");
    const SeaOrders &orders = states.sea_orders;
    Expect(orders.known && orders.active && orders.available && orders.complete && orders.scene == 6 && orders.revision == 15 && orders.activity == 2 &&
               orders.stage == "commands" && orders.recipient == "r501_1" && orders.recipients.size() == 4 && orders.items.size() == 7,
           "seaOrders: the menu is read");
    Expect(orders.items[0].source == "BI_SailTo" && !orders.items[5].enabled && orders.items[5].cooldown == 0.4 && orders.recipients[0].main, "seaOrders: items and recipients");
    bench.Feed("seaOrders", "sea-orders-idle");
    Expect(states.sea_orders.revision == 15, "seaOrders: an older revision is not news");
    bench.Feed("seaOrders", "sea-orders-unknown-kind");
    Expect(states.sea_orders.known && states.sea_orders.active && !states.sea_orders.complete && mount.errors.size() == errors,
           "seaOrders: an unknown item kind leaves the menu to the engine, without an error of the channel");
    bench.Set("seaOrders", nullptr);
    Expect(!states.sea_orders.known && mount.errors.size() == errors, "seaOrders: null = no menu");
    bad = State("sea-orders-root");
    bad["stage"] = "teleport";
    rejected("seaOrders", bad, "seaOrders: an unknown stage");
    bad = State("sea-orders-root");
    bad["items"][1]["id"] = "i0";
    rejected("seaOrders", bad, "seaOrders: a repeated item id");
    bad = State("sea-orders-root");
    bad["recipientId"] = "r9_9";
    rejected("seaOrders", bad, "seaOrders: a recipient that is not listed");
    bad = State("sea-orders-root");
    bad["selectedItemId"] = "i99";
    rejected("seaOrders", bad, "seaOrders: a selection that is not listed");
    bad = State("sea-orders-root");
    bad.erase("recipients");
    rejected("seaOrders", bad, "seaOrders: a menu without recipients");
    Expect(!states.sea_orders.known, "seaOrders: a rejected menu leaves none");

    bench.Feed("seaQuickAction", "sea-quick-board");
    Expect(states.sea_quick.known && states.sea_quick.available && states.sea_quick.action == "Board" && states.sea_quick.revision == 32 &&
               states.sea_quick.activity == 5 && states.sea_quick.label == "Абордаж",
           "seaQuickAction: the offer is read");
    bench.Feed("seaQuickAction", "sea-quick-moor");
    Expect(states.sea_quick.action == "Board", "seaQuickAction: an older revision is not news");
    bench.Feed("seaQuickAction", "sea-quick-none");
    Expect(states.sea_quick.known && !states.sea_quick.available && states.sea_quick.action.empty(), "seaQuickAction: no offer");
    bad = State("sea-quick-moor");
    bad["revision"] = 40;
    bad["action"] = "EnterToSea";
    rejected("seaQuickAction", bad, "seaQuickAction: an action outside the five");
    Expect(!states.sea_quick.known, "seaQuickAction: a rejected offer leaves none");
    bad = State("sea-quick-moor");
    bad["revision"] = 41;
    bad["label"] = "";
    rejected("seaQuickAction", bad, "seaQuickAction: an offer without its caption");
    bad = State("sea-quick-moor");
    bad.erase("activity");
    rejected("seaQuickAction", bad, "seaQuickAction: an offer without its owner");
}

static void CheckSailing()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    Sail(bench);
    const Json &shown = mount.shown.elements;
    Expect(mount.visible && mount.shown.layout == "sea" && !mount.shown.modal && mount.shown.sheet.empty(), "sailing: the sea layout");
    for (const char *id : {"vitals", "hull", "crew", "sails", "fleet", "hero", "pause", "orders", "minimap", "sail", "helm", "context", "camera", "ammo", "fire"})
        Expect(mount.Has(id), std::string("sailing shows ") + id);
    Expect(!mount.Has("spyglass") && !mount.Has("log") && !mount.Has("ammo-1"), "sailing: no spyglass off the deck, no event lines, no fan");
    Expect(shown["hull"]["value"] == 82 && shown["crew"]["value"] == 90 && shown["sails"]["value"] == 95, "the three bars");
    Expect(shown["fleet"]["ships"].size() == 3 && shown["fleet"]["ships"][0]["icon"] == "ship-brig" && shown["fleet"]["ships"][2]["hp"] == 41 &&
               shown["fleet"]["ships"][1]["label"] == "Ласточка",
           "the companions with their hulls");
    Expect(shown["minimap"]["place"] == "Мартиника" && shown["minimap"]["time"] == "09:06" && shown["minimap"]["speed"] == "4" &&
               shown["minimap"]["wind"] == "9" && shown["minimap"]["native"] == true,
           "the minimap readouts: " + shown["minimap"].dump());
    Expect(shown["sail"]["value"] == 2 && shown["ammo"]["badge"] == "88" && shown["ammo"]["badgeTone"] == "gold" && shown["ammo"]["icon"] == "ammo" &&
               !shown["fire"].contains("fill"),
           "sails, ammunition and loaded guns");
    Expect(shown["context"]["icon"] == "moor" && shown["context"]["label"] == "Причалить" && shown["context"]["variant"] == "primary", "the quick action with its own glyph");
    Expect(mount.errors.empty(), "sailing: no errors");

    // nothing is acknowledged, and nothing can be activated, before a frame is drawn
    Expect(!Sent(mount, "shiphud:9\t2047\t3") && !Sent(mount, "seaquick:1\t5\t31\tack\t1"), "a built view is not a drawn one");
    bench.Press("context");
    Expect(!Sent(mount, "seaquick:1\t5\t31\tactivate\tMoor"), "a quick action that was not drawn yet cannot be tapped");
    bench.Frame();
    Expect(Sent(mount, "shiphud:9\t2047\t3"), "the drawn elements replace the engine's indicators: hull, crew, sails, ammunition, fleet, speed, wind, date, place");
    Expect(Count(mount, "hudowner:sea") == 1 && Count(mount, "hudowner:worldmap") == 1 && !Sent(mount, "hudowner:land"),
           "the HUD declares once which engine HUDs it presents (the engine then keeps their unanswered revisions native-free)");
    Expect(mount.slots.back() == "seaquick-ack" && mount.Last() == "seaquick:1\t5\t31\tack\t1", "the drawn quick action is acknowledged: " + mount.Last());
    bench.Frame(0.1);
    bench.Frame(5.0);
    Expect(Count(mount, "shiphud:9\t2047\t3") == 1 && Count(mount, "seaquick:1\t5\t31\tack\t1") == 1,
           "the indicator mask and the quick action are said once per revision, however far apart frames are");

    // shortcuts and tap controls
    bench.Press("hero");
    Expect(mount.Last() == "shortcut:character", "hero: " + mount.Last());
    bench.Press("pause");
    Expect(mount.Last() == "shortcut:mainmenu", "menu: " + mount.Last());
    bench.Press("camera");
    Expect(mount.Last() == "gameplay:9\tSea_CameraSwitch", "camera: " + mount.Last());
    bench.Press("fire");
    Expect(mount.Last() == "gameplay:9\tShip_Fire", "fire: " + mount.Last());
    Expect(mount.shown.elements["time"]["badge"] == "×1" && mount.shown.elements["time"]["variant"] == "normal" && mount.Has("time-slower") && mount.Has("time-faster"),
           "the time scale the engine runs at: " + mount.shown.elements["time"].dump());
    bench.Press("time");
    Expect(mount.Last() == "gameplay:9\tTimeScale", "the time button is the game's R: " + mount.Last());
    bench.Press("time-faster");
    Expect(mount.Last() == "gameplay:9\tTimeScaleFaster", "faster: " + mount.Last());
    bench.Press("time-slower");
    Expect(mount.Last() == "gameplay:9\tTimeScaleSlower", "slower: " + mount.Last());
    bench.Press("orders");
    Expect(mount.Last() == "orders:1\t6\t14\t2\topen\t", "orders: " + mount.Last());
    bench.Press("fleet", "ship", "1");
    Expect(mount.Last() == "orders:1\t6\t14\t2\topen\tr503_8", "a tap on a companion opens the orders for it: " + mount.Last());
    const size_t before = mount.commands.size();
    bench.Press("fleet", "ship", "7");
    Expect(mount.commands.size() == before, "a companion that is not there opens nothing");

    // the quick action: exactly this action, once
    bench.Press("context");
    Expect(mount.Last() == "seaquick:1\t5\t31\tactivate\tMoor", "the quick action activates exactly its action: " + mount.Last());
    bench.Press("context");
    Expect(Count(mount, "seaquick:1\t5\t31\tactivate\tMoor") == 1, "a second tap on the same offer does nothing");
    bench.Feed("seaQuickAction", "sea-quick-board");
    Expect(mount.shown.elements["context"]["icon"] == "grapple" && mount.shown.elements["context"]["variant"] == "combat", "another offer, another glyph");
    bench.Press("context");
    Expect(!Sent(mount, "seaquick:1\t5\t32\tactivate\tBoard"), "the new offer is not tapped before it is drawn");
    bench.Frame(0.3);
    Expect(mount.Last() == "seaquick:1\t5\t32\tack\t1" && !Sent(mount, "seaquick:1\t5\t31\tack\t0"), "the new offer is acknowledged; the replaced one needs no word");
    bench.Press("context");
    Expect(mount.Last() == "seaquick:1\t5\t32\tactivate\tBoard", "board: " + mount.Last());
    bench.Feed("seaQuickAction", "sea-quick-none");
    bench.Frame();
    Expect(!mount.Has("context") && !Sent(mount, "seaquick:1\t5\t33\tack\t1"), "no offer, no button, nothing covered");

    // the helm holds a turn; the engine hears from it at least every 100 ms
    bench.Press("helm", "change", "15");
    Expect(mount.Last() != "hold:9\tShip_TurnRight\t1", "a helm near the middle turns nothing");
    bench.Press("helm", "change", "60");
    Expect(mount.Last() == "hold:9\tShip_TurnRight\t1" && mount.slots.back() == "hold-Ship_TurnRight", "helm to starboard: " + mount.Last());
    bench.Frame(0.12);
    Expect(Count(mount, "hold:9\tShip_TurnRight\t1") == 2, "a held helm is repeated");
    bench.Press("helm", "change", "-80");
    Expect(Sent(mount, "hold:9\tShip_TurnRight\t0") && mount.Last() == "hold:9\tShip_TurnLeft\t1", "helm over to port: " + mount.Last());
    bench.Press("helm", "change", "0");
    Expect(mount.Last() == "hold:9\tShip_TurnLeft\t0", "helm amidships: " + mount.Last());
    bench.Press("helm", "change", "100");
    bench.hud.Released();
    Expect(mount.Last() == "hold:9\tShip_TurnRight\t0", "cancelled input lets the helm go: " + mount.Last());
    const size_t held = mount.commands.size();
    bench.Frame(0.3);
    Expect(mount.commands.size() == held, "a released helm is silent");

    // the sail lever
    bench.Press("sail", "change", "1");
    Expect(mount.Last() == "sails:9\t1" && mount.slots.back() == "sails" && mount.shown.elements["sail"]["value"] == 1, "sails to half: " + mount.Last());
    bench.Frame(2.0);
    Expect(mount.shown.elements["sail"]["value"] == 2, "sails that did not follow: the lever returns to what the ship has");
    bench.Press("sail", "change", "5");
    Expect(mount.Last() == "sails:9\t1", "a position the lever does not have is not sent");

    // the ammunition fan
    bench.Feed("seaQuickAction", "sea-quick-moor");
    Json newer = State("sea-quick-moor");
    newer["revision"] = 34;
    bench.Set("seaQuickAction", newer);
    bench.Press("ammo");
    const Json &fan = mount.shown.elements;
    Expect(mount.Has("ammo-1") && mount.Has("ammo-2") && mount.Has("ammo-3") && fan["ammo-1"]["icon"] == "grape" && fan["ammo-1"]["badge"] == "40" &&
               fan["ammo-2"]["icon"] == "chain" && fan["ammo-3"]["icon"] == "bomb" && fan["ammo-3"]["badge"] == "16" && fan["ammo"]["variant"] == "active",
           "the fan offers the other three kinds with their counts");
    Expect(!mount.Has("context") && !mount.Has("camera") && mount.Has("fire") && mount.Has("helm"), "the fan takes the place of the buttons around it");
    bench.Frame(0.3);
    Expect(!Sent(mount, "seaquick:1\t5\t34\tack\t0") && mount.Last() == "seaquick:1\t5\t34\tack\t1", "the quick action stays covered while the fan is open: " + mount.Last());
    bench.Press("ammo-2");
    Expect(mount.Last() == "gameplay:9\thk_charge3" && !mount.Has("ammo-1") && mount.Has("context"), "a kind is chosen and the fan folds: " + mount.Last());
    bench.Press("ammo");
    bench.Frame(7.0);
    Expect(!mount.Has("ammo-1"), "an untouched fan folds by itself");
    bench.Press("ammo");
    bench.Press("ammo");
    Expect(!mount.Has("ammo-1"), "a second tap folds the fan");

    // the minimap: the engine's radar needs the place of the disc, then the word that it shows
    Expect(!Sent(mount, "navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1"), "no aperture before the disc is on the screen");
    mount.boxes["minimap .mm-disc"] = {654, 34, 100, 100, 0, 0, 800, 400, 1};
    bench.Frame();
    Expect(mount.Last() == "navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1" && mount.slots.back() == "navigator-prepare",
           "the aperture of the drawn disc: " + mount.Last());
    bench.Frame(5.0);
    Expect(Count(mount, "navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1") == 1 && !Sent(mount, "navigator:9\t3\t4\tvisible\t1"),
           "the aperture is said once per owner and place; nothing is called visible before the engine drew there");
    Json drawn = State("ship");
    drawn["sequence"] = 22;
    drawn["navigatorLayoutId"] = 1;
    bench.Set("ship", drawn);
    bench.Frame(0.3);
    Expect(Sent(mount, "navigator:9\t3\t4\tvisible\t1"), "the engine drew into the aperture: the readouts are called visible");
    bench.Press("minimap", "zoom");
    Expect(mount.Last() == "navigator:9\t3\t4\tin", "a tap on the minimap zooms: " + mount.Last());
    drawn["sequence"] = 23;
    drawn["navigatorZoomIn"] = false;
    bench.Set("ship", drawn);
    bench.Press("minimap", "zoom");
    Expect(mount.Last() == "navigator:9\t3\t4\tout", "at the closest scale the next tap goes back out: " + mount.Last());
    mount.boxes["minimap .mm-disc"] = {554, 34, 100, 100, 0, 0, 800, 400, 1};
    bench.Frame(0.01);
    Expect(mount.Last() == "navigator:9\t3\t4\tprepare\t2\t800\t400\t604\t84\t50\t1000\t1", "a disc that moved is a new aperture, at once: " + mount.Last());

    // event lines; a fleet that wraps moves their band
    Expect(bench.hud.Notice("Якорь поднят", "info") && (bench.hud.Tick(), mount.Has("log")) && !mount.shown.elements["log"].contains("safeLeft"), "event lines top-centre");
    mount.boxes["fleet .item.ship:first-child"] = {10, 60, 52, 52, 0, 0, 800, 400, 1};
    mount.boxes["fleet .item.ship:last-child"] = {130, 60, 52, 52, 0, 0, 800, 400, 1};
    bench.Frame();
    bench.Frame();
    Expect(mount.shown.elements["log"]["safeLeft"] == 190, "the band of event lines begins after the last fleet column: " + mount.shown.elements["log"].dump());

    // what is not drawn any more gives the engine's indicators back
    mount.commands.clear();
    bench.Feed("presentation", "presentation-menu");
    Expect(!mount.visible && Sent(mount, "shiphud:9\t0\t3") && Sent(mount, "navigator:9\t3\t4\tclear") && Sent(mount, "seaquick:1\t5\t34\tack\t0"),
           "under a menu every acknowledgement is withdrawn");
    mount.commands.clear();
    bench.Feed("presentation", "presentation-game");
    bench.Frame();
    Expect(Sent(mount, "shiphud:9\t2047\t3") && Sent(mount, "seaquick:1\t5\t34\tack\t1"), "back in the game they are drawn and acknowledged again");
    bench.Press("helm", "change", "100");
    mount.commands.clear();
    bench.Feed("presentation", "presentation-background");
    Expect(Sent(mount, "hold:9\tShip_TurnRight\t0") && Sent(mount, "shiphud:9\t0\t3") && Sent(mount, "seaquick:1\t5\t34\tack\t0"),
           "a game that lost the screen: the helm is let go and nothing stays covered");
    mount.commands.clear();
    bench.Press("fire");
    bench.Press("context");
    bench.Press("helm", "change", "100");
    Expect(mount.commands.empty(), "not live: no input goes out");

    // states that take elements away
    bench.Feed("presentation", "presentation-game");
    bench.Feed("ship", "ship-none");
    Expect(mount.visible && !mount.Has("vitals") && !mount.Has("fleet") && !mount.Has("minimap") && !mount.Has("sail") && !mount.Has("ammo") && mount.Has("fire") &&
               mount.Has("helm"),
           "without telemetry only the controls of the gameplay state stay");
    Json other = State("ship");
    other["sequence"] = 30;
    other["gameplayRevision"] = 8;
    bench.Set("ship", other);
    Expect(!mount.Has("vitals"), "the ship of another gameplay revision is not shown");
    Json helmless = State("gameplay-sailing");
    helmless["revision"] = 12;
    helmless["capabilities"] = Json::array({"Ship_Fire"});
    const size_t errors = mount.errors.size();
    bench.Set("gameplay", helmless);
    Expect(!mount.Has("helm") && !mount.Has("camera") && mount.Has("fire") && mount.errors.size() == errors + 1, "a gameplay without a helm says so and shows none");
}

static void CheckBattle()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Feed("seaOrders", "sea-orders-idle");
    bench.Feed("gameplay", "gameplay-naval");
    bench.Feed("ship", "ship-battle");
    bench.Feed("seaQuickAction", "sea-quick-board");
    const Json &shown = mount.shown.elements;
    Expect(mount.shown.layout == "sea-battle" && mount.Has("spyglass") && shown["fleet"]["ships"].size() == 7, "battle: its layout, the spyglass on deck, seven companions");
    Expect(shown["fire"]["fill"] == 65 && shown["fire"]["fillTone"] == "reload" && shown["fire"]["reloading"] == true, "guns that reload fill the fire button: " + shown["fire"].dump());
    Expect(shown["ammo"]["badge"] == "12" && shown["ammo"]["badgeTone"] == "danger" && shown["hull"]["value"] == 27 && shown["sail"]["value"] == 1 &&
               shown["minimap"]["time"] == "11:42",
           "battle values");
    Expect(shown["time"]["badge"] == "×1.5" && shown["time"]["variant"] == "active", "an accelerated world: " + shown["time"].dump());
    Expect(shown["effect-0"]["icon"] == "perk-instantrepair" && !shown["effect-0"].contains("fill") && !shown["effect-0"].contains("disabled") &&
               shown["effect-1"]["fill"] == 40 && shown["effect-1"]["fillTone"] == "cooldown",
           "the battle abilities: " + shown["effect-0"].dump() + shown["effect-1"].dump());
    bench.Press("effect-0");
    Expect(mount.Last() == "gameplay:10\tBI_InstantRepair", "a ready ability is launched: " + mount.Last());
    const size_t launched = mount.commands.size();
    bench.Press("effect-1");
    Expect(mount.commands.size() == launched, "a recovering ability does nothing");
    bench.Press("spyglass", "down");
    Expect(mount.Last() == "hold:10\tTelescopeIn\t1", "the spyglass is held: " + mount.Last());
    bench.Press("spyglass", "up");
    Expect(mount.Last() == "hold:10\tTelescopeIn\t0", "… and lowered: " + mount.Last());

    // a helm held through sailing ↔ battle keeps turning in the new gameplay revision
    bench.Press("helm", "change", "-100");
    Expect(mount.Last() == "hold:10\tShip_TurnLeft\t1", "helm in battle: " + mount.Last());
    bench.Feed("gameplay", "gameplay-sailing");
    bench.Frame(0.12);
    Expect(mount.shown.layout == "sea" && Sent(mount, "hold:9\tShip_TurnLeft\t1"), "the held helm follows the gameplay revision");
}

static void CheckOrders()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    Sail(bench);
    bench.Feed("gameplay", "gameplay-naval");
    bench.Frame();
    bench.Press("helm", "change", "100");
    bench.Press("orders");
    Expect(Sent(mount, "hold:10\tShip_TurnRight\t0") && mount.Last() == "orders:1\t6\t14\t2\topen\t", "opening the orders lets the helm go first");

    // the engine opens its menu and blocks gameplay: the panel stays on the last layout
    bench.Feed("seaOrders", "sea-orders-root");
    bench.Feed("gameplay", "gameplay-orders");
    bench.Feed("ship", "ship-orders");
    const Json &props = mount.shown.sheet_props;
    Expect(mount.visible && mount.shown.layout == "sea-battle" && mount.shown.sheet == "s-hud-orders" && mount.shown.modal, "the orders panel is a modal sheet over the sea");
    Expect(mount.Has("vitals") && !mount.Has("helm") && !mount.Has("fire") && mount.Has("minimap") && mount.shown.elements["minimap"]["readout"] == false, "under the panel the bars and the radar stay");
    Expect(props["title"] == "hud.orders.title" && props["back"] == false && props["crumb"] == "" && props["recipients"].size() == 4 && props["tiles"].size() == 5 &&
               props["rows"].size() == 1,
           "first stage: ships, order tiles, ability rows: " + props.dump());
    Expect(props["recipients"][0]["selected"] == true && props["recipients"][0]["meta"] == "hud.orders.flagship" && props["recipients"][0]["value"] == "82%" &&
               props["recipients"][1]["image"]["icon"] == "ship-brig" && !props["recipients"][0].contains("bars"),
           "the flagship is chosen");
    Expect(props["tiles"][0]["image"]["icon"] == "sail-to" && props["tiles"][1]["image"]["icon"] == "map" && props["tiles"][3]["image"]["icon"] == "cabin" && props["rows"][0]["disabled"] == true &&
               props["rows"][0]["cooldown"] == 60,
           "orders have their icons, a cooling ability its share");
    Expect(!Sent(mount, "orders:1\t6\t15\t2\tack\t1"), "a built panel is not acknowledged");
    bench.Frame();
    Expect(mount.Last() == "orders:1\t6\t15\t2\tack\t1" && mount.slots.back() == "orders-ack", "the drawn panel is acknowledged: " + mount.Last());
    Expect(Sent(mount, "shiphud:11\t51\t3"), "under the panel the bars and the fleet (the panel lists it) replace the engine's");

    // a tap acts at once: select, then activate against the revision that confirms the selection
    bench.Press("item-i1");
    Expect(mount.Last() == "orders:1\t6\t15\t2\tselectItem\ti1" && mount.shown.sheet_props["tiles"][1]["pressed"] == true, "a tap selects: " + mount.Last());
    bench.Press("item-i0");
    Expect(mount.Last() == "orders:1\t6\t15\t2\tselectItem\ti1", "a second tap while the first is on its way does nothing");
    bench.Feed("seaOrders", "sea-orders-root-selected");
    bench.Frame();
    Expect(Sent(mount, "orders:1\t6\t16\t2\tack\t1") && mount.Last() == "orders:1\t6\t16\t2\tactivateItem\ti1", "… and activates once the engine selected it: " + mount.Last());
    bench.Press("item-i5");
    Expect(mount.Last() == "orders:1\t6\t16\t2\tactivateItem\ti1", "a cooling ability cannot be chosen");

    // another ship; its bars under its row
    bench.Frame(2.0);
    bench.Press("recipient-r502_7");
    Expect(mount.Last() == "orders:1\t6\t16\t2\tselectRecipient\tr502_7", "a tap on a ship: " + mount.Last());
    bench.Feed("seaOrders", "sea-orders-companion");
    Expect(mount.shown.sheet_props["recipients"][1]["selected"] == true && mount.shown.sheet_props["recipients"][1]["bars"]["hull"] == 74 &&
               mount.shown.sheet_props["tiles"][2]["image"]["icon"] == "follow" && mount.shown.sheet_props["rows"][0]["image"]["icon"] == "brander",
           "the companion's orders and bars");
    bench.Press("recipient-r502_7");
    Expect(mount.Last() == "orders:1\t6\t16\t2\tselectRecipient\tr502_7", "the chosen ship is not chosen again");

    // nested stages: back; targets are rows
    bench.Feed("seaOrders", "sea-orders-targets");
    Expect(mount.shown.sheet_props["back"] == true && mount.shown.sheet_props["crumb"] == "Атаковать" && mount.shown.sheet_props["tiles"].empty() &&
               mount.shown.sheet_props["rows"].size() == 3 && mount.shown.sheet_props["rows"][2]["image"]["icon"] == "fort",
           "targets: " + mount.shown.sheet_props.dump());
    const size_t before = mount.commands.size();
    bench.Press("recipient-r503_8");
    Expect(mount.commands.size() == before, "ships are not switched in the middle of an order");
    Expect(bench.hud.Back() && mount.Last() == "orders:1\t6\t18\t2\tback\t", "the back key steps back: " + mount.Last());
    bench.Press("sheet-back");
    Expect(mount.Last() == "orders:1\t6\t18\t2\tback\t", "the ribbon steps back");
    bench.Feed("seaOrders", "sea-orders-options");
    Expect(mount.shown.sheet_props["tiles"].size() == 4 && mount.shown.sheet_props["tiles"][1]["image"]["icon"] == "grape" && mount.shown.sheet_props["tiles"][3]["disabled"] == true,
           "options: the charges with the ammunition icons");
    bench.Press("sheet", "close");
    Expect(mount.Last() == "orders:1\t6\t19\t2\tclose\t", "a tap on the scrim closes: " + mount.Last());

    // a menu only the engine can draw stays the engine's
    const size_t errors = mount.errors.size();
    bench.Feed("seaOrders", "sea-orders-unknown-kind");
    bench.Frame();
    Expect(mount.shown.sheet.empty() && !mount.shown.modal && mount.errors.size() == errors + 1 && !Sent(mount, "orders:1\t6\t20\t2\tack\t1") && !bench.hud.Back(),
           "an undrawable menu: an error, no panel, no acknowledgement");

    // the game loses the screen with the panel open
    Json again = State("sea-orders-root");
    again["revision"] = 21;
    bench.Set("seaOrders", again);
    bench.Frame();
    Expect(mount.Last() == "orders:1\t6\t21\t2\tack\t1", "drawn again");
    mount.commands.clear();
    bench.Feed("presentation", "presentation-background");
    Expect(Sent(mount, "orders:1\t6\t21\t2\tack\t0") && mount.Last() == "orders:1\t6\t21\t2\tclose\t", "not live: the coverage is withdrawn and the menu closed");

    // the menu closes: the sea HUD is back
    bench.Feed("presentation", "presentation-game");
    Json idle = State("sea-orders-idle");
    idle["revision"] = 22;
    bench.Set("seaOrders", idle);
    bench.Feed("gameplay", "gameplay-sailing");
    bench.Feed("ship", "ship");
    Expect(mount.shown.sheet.empty() && !mount.shown.modal && mount.Has("helm") && mount.Has("orders") && mount.shown.layout == "sea", "the panel closed");
}

static void CheckTouches(const std::string &pack)
{
    Runtime run(pack);
    if (!run.host)
        return;
    Host &host = *run.host;
    run.Feed("presentation", "presentation-game");
    run.Feed("seaOrders", "sea-orders-idle");
    run.Feed("gameplay", "gameplay-sailing");
    run.Feed("ship", "ship");
    run.Feed("seaQuickAction", "sea-quick-moor");
    run.Frame();
    run.Frame();
    Expect(!HostOwnsHud("sailing") && !HostOwnsHud("naval"), "the sea HUD is drawn here");

    // The test layout (800 × 400): helm 20…220 × 309…373, fire 708…784 × 308…384, minimap disc
    // 654…754 × 34…134, sail lever 732…788 × 154…298, context 640…704 × 226…290, the first
    // companion 10…62 × 60…112, vitals 10…210 × 10…50.
    Expect(host.WantsTouch(120, 341) && host.WantsTouch(746, 346) && host.WantsTouch(704, 84) && host.WantsTouch(760, 226) && host.WantsTouch(672, 258) &&
               host.WantsTouch(36, 86),
           "helm, fire, minimap, lever, quick action and companions are the UI's");
    Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(100, 30) && !host.WantsTouch(300, 341) && !host.WantsTouch(700, 150),
           "the open sea, the bars and the minimap readouts are the game's");
    Expect(run.Sent("navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1"), "the aperture is the disc as it is laid out");
    Expect(run.Sent("shiphud:9\t2047\t3") && run.Sent("seaquick:1\t5\t31\tack\t1"), "the first drawn frame acknowledges what it shows");

    // the helm under one finger, the camera free, the guns under another
    host.Touch(TouchPhase::Down, 1, 120, 341);
    host.Touch(TouchPhase::Move, 1, 180, 341);
    Expect(run.Last() == "hold:9\tShip_TurnRight\t1", "the helm turns the ship: " + run.Last());
    Expect(!host.WantsTouch(400, 200), "a held helm leaves the sea to the camera finger");
    host.Touch(TouchPhase::Down, 2, 746, 346);
    host.Touch(TouchPhase::Up, 2, 746, 346);
    Expect(run.Last() == "gameplay:9\tShip_Fire", "a second finger fires: " + run.Last());
    run.Frame(0.12);
    Expect(run.Last() == "hold:9\tShip_TurnRight\t1", "the helm is still held (heartbeat): " + run.Last());
    host.Touch(TouchPhase::Down, 2, 760, 226);
    Expect(run.Last() == "sails:9\t1", "the lever under a second finger: " + run.Last());
    host.Touch(TouchPhase::Move, 2, 760, 290);
    Expect(run.Last() == "sails:9\t0", "… dragged down furls the sails: " + run.Last());
    host.Touch(TouchPhase::Up, 2, 760, 290);
    host.Touch(TouchPhase::Move, 1, 30, 341);
    Expect(run.Sent("hold:9\tShip_TurnRight\t0") && run.Last() == "hold:9\tShip_TurnLeft\t1", "the helm goes over: " + run.Last());

    // the helm survives sailing → battle (the element is patched, not remounted)
    run.Feed("gameplay", "gameplay-naval");
    run.Feed("ship", "ship-battle");
    run.Frame(0.12);
    Expect(run.Sent("hold:10\tShip_TurnLeft\t1"), "the held helm survives the change of layout");
    host.Touch(TouchPhase::Up, 1, 30, 341);
    Expect(run.Last() == "hold:10\tShip_TurnLeft\t0", "letting go straightens the helm: " + run.Last());

    // taps: minimap, quick action, companions
    run.Feed("seaQuickAction", "sea-quick-board");
    run.Frame();
    host.Touch(TouchPhase::Down, 1, 704, 84);
    host.Touch(TouchPhase::Up, 1, 704, 84);
    Expect(run.Last() == "navigator:10\t3\t4\tin", "a tap on the disc zooms: " + run.Last());
    host.Touch(TouchPhase::Down, 1, 672, 258);
    host.Touch(TouchPhase::Up, 1, 672, 258);
    Expect(run.Last() == "seaquick:1\t5\t32\tactivate\tBoard", "a tap on the quick action: " + run.Last());
    host.Touch(TouchPhase::Down, 1, 672, 258);
    host.Touch(TouchPhase::Up, 1, 672, 258);
    Expect(run.Last() == "seaquick:1\t5\t32\tactivate\tBoard" && !run.Sent("seaquick:1\t5\t31\tactivate\tMoor"), "one activation per offer");
    host.Touch(TouchPhase::Down, 1, 36, 146);
    host.Touch(TouchPhase::Up, 1, 36, 146);
    Expect(run.Last() == "orders:1\t6\t14\t2\topen\tr503_8", "a tap on the second companion: " + run.Last());

    // the orders panel is modal
    run.Feed("seaOrders", "sea-orders-root");
    run.Feed("gameplay", "gameplay-orders");
    run.Feed("ship", "ship-orders");
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(400, 200) && host.WantsTouch(5, 5) && host.WantsBack() && run.Sent("orders:1\t6\t15\t2\tack\t1"), "the open panel takes every touch and the back key");
    host.Touch(TouchPhase::Down, 1, 700, 70); // the second tile
    host.Touch(TouchPhase::Up, 1, 700, 70);
    Expect(run.Last() == "orders:1\t6\t15\t2\tselectItem\ti1", "a tap on an order: " + run.Last());
    host.Back();
    Expect(run.Last() == "orders:1\t6\t15\t2\tclose\t", "the back key closes the first stage: " + run.Last());
    host.Touch(TouchPhase::Down, 1, 400, 300); // beside everything
    host.Touch(TouchPhase::Up, 1, 400, 300);
    Expect(run.Last() == "orders:1\t6\t15\t2\tclose\t", "a tap on the scrim closes");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    const std::string pack = argc > 2 ? argv[2] : "tests/pack";
    CheckConsumers();
    CheckSailing();
    CheckBattle();
    CheckOrders();
    CheckTouches(pack);
    if (failures)
        std::fprintf(stderr, "%d sea HUD check(s) failed\n", failures);
    else
        std::printf("sea HUD checks passed\n");
    return failures ? 1 : 0;
}
