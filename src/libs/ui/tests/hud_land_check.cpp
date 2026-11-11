// Host checks for the land HUD. First the scene alone (a recording mount): what each
// synthetic state shows and which engine input each event sends. Then the whole runtime
// with a renderer that draws nothing and a small synthetic pack (tests/pack): which
// touches the UI claims over the game, per pointer, and what real gestures send.
// No GL context, no game data.
#include "hud_bench.h"

static void CheckExplore()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    ExpectEqual(mount.shown.layout, "land", "explore uses the land layout");
    Expect(mount.shown.elements["health"]["value"] == 84 && mount.shown.elements["energy"]["value"] == 62 && mount.Has("vitals"), "vitals in percent");
    ExpectEqual(mount.shown.elements["location"]["text"].get<std::string>(), "Сен-Пьер · улицы", "the location line is town · place, without island, date or time");
    bench.Set("worldInfo", Json("Открытое море\n7 окт. 1654\n12:31"));
    ExpectEqual(mount.shown.elements["location"]["text"].get<std::string>(), "Открытое море", "a single name stands alone");
    bench.Set("worldInfo", Json("1 октября 1654 11:15"));
    Expect(!mount.Has("location"), "a date alone is no location");
    bench.Feed("worldInfo", "world-info");
    Expect(mount.Has("hero") && mount.shown.elements["hero"]["disabled"] == false && mount.Has("pause") && mount.Has("move") && mount.Has("actions") &&
               mount.Has("potion") && mount.Has("weapon") && !mount.Has("context") && !mount.Has("strike") && !mount.shown.modal,
           "the peace set; no context without an action");
    ExpectEqual(mount.shown.elements["weapon"]["label"].get<std::string>(), "hud.weapon.draw", "the weapon button names what it will do");

    bench.Press("hero");
    ExpectEqual(mount.Last(), "shortcut:character", "Персонаж opens the character");
    bench.Press("pause");
    ExpectEqual(mount.Last(), "shortcut:mainmenu", "Меню opens the menu");
    bench.Press("weapon");
    ExpectEqual(mount.Last(), "combat:2\t5\t11\t1\tweapon\ttap", "the weapon tap carries the activity, the action's generation and a sequence");
    bench.Press("potion");
    ExpectEqual(mount.Last(), "combat:2\t5\t12\t2\tpotion\ttap", "the sequence grows");
    bench.Press("actions");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t10\t2\topen\t", "Действия asks the engine to open its menu");

    // the script's quick action
    bench.Feed("contextAction", "context-door");
    Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "door" && mount.shown.elements["context"]["label"] == "hud.context.enter",
           "a door in front: the context slot");
    bench.Press("context");
    ExpectEqual(mount.Last(), "context:Reload", "the context tap names the action the engine published");
    bench.Set("contextAction", Json("Talk"));
    Expect(!mount.Has("context"), "the script's Talk never shows: talking belongs to the engine's target");
    bench.Set("contextAction", Json("Nothing"));
    Expect(!mount.Has("context"), "Nothing is no action");
    bench.Set("contextAction", Json("Levitate"));
    Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "hand" && !mount.logs.empty() &&
               mount.logs.back() == "land HUD: context action 'Levitate' has no icon of its own",
           "an unknown action keeps its button with the generic look, and is logged");
    bench.Press("context");
    ExpectEqual(mount.Last(), "context:Levitate", "… and still acts");

    // talking: the engine's own target (U0): appears and disappears with the combat state
    bench.Feed("combat", "combat-explore-talk");
    Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "talk" && mount.shown.elements["context"]["label"] == "hud.context.talk",
           "somebody to talk to: the talk button takes the slot");
    bench.Press("context");
    ExpectEqual(mount.Last(), "combat:2\t5\t14\t3\tinteract\ttap", "talk is the engine's interact action");
    bench.Press("weapon");
    ExpectEqual(mount.Last(), "combat:2\t5\t11\t4\tweapon\ttap", "a passer-by changes neither the activity nor the weapon's generation");
    const size_t sent = mount.commands.size();
    bench.router.SetState("combat", State("combat-explore")); // the target is lost; the tap arrives before the next build
    bench.hud.OnEvent({"context", "click", ""});
    Expect(mount.commands.size() == sent, "a tap after the target is lost sends nothing");
    bench.hud.Tick();
    bench.Set("contextAction", Json(""));
    Expect(!mount.Has("context"), "the target left: no button");

    // a combat state of another revision is not ready: buttons stay, disabled, and send nothing
    Json stale = State("combat-explore");
    stale["gameplayRevision"] = 9;
    stale["activityGeneration"] = 6;
    bench.Set("combat", stale);
    Expect(mount.Has("weapon") && mount.shown.elements["weapon"]["disabled"] == true, "another revision: the weapon button is disabled");
    const size_t before = mount.commands.size();
    bench.Press("weapon");
    Expect(mount.commands.size() == before, "… and its tap sends nothing");

    // no vitals, no location, no menu
    bench.Set("vitals", Json(nullptr));
    bench.Set("worldInfo", Json(""));
    bench.Set("landCommands", Json(nullptr));
    Expect(!mount.Has("vitals") && !mount.Has("health") && !mount.Has("location") && !mount.Has("actions"), "absent states take their elements away");

    // without analog movement there is no stick, and that is said
    Json keyboard = State("gameplay-explore");
    keyboard["analogMovement"] = false;
    bench.Set("gameplay", keyboard);
    Expect(!mount.Has("move") && !mount.errors.empty() && mount.errors.back().find("no analog movement") != std::string::npos, "no analog movement: no stick, an error");
}

static void CheckMovement()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    mount.commands.clear();
    bench.Press("move", "change", "0,-100");
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t1\t1\t0\t1000", "stick up = forward, full travel");
    Expect(mount.slots.back() == "move", "the stick uses its latest-value slot");
    bench.Press("move", "change", "50,50");
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t2\t1\t500\t-500", "right and back");
    const size_t count = mount.commands.size();
    bench.Frame(0.5);
    bench.Frame(5.0);
    Expect(mount.commands.size() == count && bench.hud.Tick() > 5.0,
           "a held stick is said once (the engine holds it until its release): no heartbeat, however far apart frames are");

    // explore → melee keeps the stick: same owner and generation, nothing to say
    bench.Melee();
    bench.Frame(0.11);
    Expect(mount.commands.size() == count, "the stick survives the stance change without a word");
    // a new movement session (the engine's generation moved on) while the stick is held: said again in it
    Json renewed = State("gameplay-melee");
    renewed["movementGeneration"] = 4;
    bench.Set("gameplay", renewed);
    bench.Frame();
    ExpectEqual(mount.Last(), "move:1\ttown:42\t4\t3\t1\t500\t-500", "the held stick goes on in the new generation");

    bench.Press("move", "change", "0,0");
    ExpectEqual(mount.Last(), "move:1\ttown:42\t4\t4\t0\t0\t0", "release stops the hero");
    const size_t stopped = mount.commands.size();
    bench.Frame(0.5);
    Expect(mount.commands.size() == stopped, "no heartbeat after the release");

    // the platform cancels input while the stick is held
    bench.Press("move", "change", "100,0");
    bench.hud.Released();
    Expect(mount.commands.size() >= 2 && mount.commands[mount.commands.size() - 2] == "move:1\ttown:42\t4\t6\t0\t0\t0", "cancelled input stops the hero");
    ExpectEqual(mount.Last(), "combat:2\t6\t0\t1\tall\tcancel", "… and cancels every combat hold");

    // in the background nothing moves
    bench.Feed("presentation", "presentation-background");
    const size_t quiet = mount.commands.size();
    bench.Press("move", "change", "100,0");
    bench.Frame(0.3);
    Expect(mount.commands.size() == quiet, "not live: the stick sends nothing");

    bench.Feed("presentation", "presentation-game");
    Expect(mount.visible, "a free game shows the HUD");
    bench.Feed("presentation", "presentation-loading"); // the engine's progress view (location load, transition)
    Expect(!mount.visible && mount.Last() == "presence:hidden", "loading: no HUD over the loading screen");
    bench.Feed("presentation", "presentation-game");
    Expect(mount.visible && mount.Last() == "presence:shown", "loaded: the HUD is back");
    bench.Press("move", "change", "sideways");
    Expect(!mount.errors.empty() && mount.errors.back().find("expected 'x,y'") != std::string::npos, "a malformed stick value is an error");
}

static void CheckCombat()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Melee();
    ExpectEqual(mount.shown.layout, "land-combat", "melee uses the combat layout");
    Expect(mount.Has("strike") && mount.Has("block") && mount.Has("shot") && mount.Has("actions") && mount.Has("potion") && mount.Has("weapon") &&
               !mount.Has("context") && !mount.Has("moves") && !mount.Has("dash") && !mount.Has("officers"),
           "the combat set; no wheel until the strike is held; nothing without data");
    Expect(mount.shown.elements["hero"]["disabled"] == true, "no character screen in a fight");
    ExpectEqual(mount.shown.elements["weapon"]["label"].get<std::string>(), "hud.weapon.sheathe", "the weapon button sheathes now");
    Expect(mount.shown.elements["shot"]["badge"] == "1" && mount.shown.elements["shot"]["disabled"] == false && !mount.shown.elements["shot"].contains("fill"),
           "a loaded pistol: charges as the badge, no fill");

    mount.commands.clear();
    bench.Press("shot");
    ExpectEqual(mount.Last(), "combat:2\t6\t21\t1\tpistol\ttap", "the shot");
    bench.Press("block", "down");
    ExpectEqual(mount.Last(), "combat:2\t6\t22\t2\tblock\tdown", "block is held …");
    bench.Press("block", "up");
    ExpectEqual(mount.Last(), "combat:2\t6\t22\t3\tblock\tup", "… and let go");
    const size_t count = mount.commands.size();
    bench.Press("block", "click");
    bench.Press("strike", "click");
    Expect(mount.commands.size() == count && mount.errors.empty(), "the press of a held or swiped button needs no action");

    // the wheel
    bench.Press("strike", "open", "fast");
    Expect(mount.Has("moves") && mount.shown.elements["moves"]["selected"] == "centre" && mount.shown.elements["moves"]["caption"] == "hud.move.fast" &&
               mount.shown.elements["moves"]["moves"].size() == 6,
           "holding the strike opens the wheel on the fast attack");
    Expect(mount.shown.dimmed == std::vector<std::string>({"strike", "block", "shot", "potion", "weapon"}), "the combat cluster that is shown is dimmed under the wheel");
    bench.Press("strike", "preview", "break");
    Expect(mount.shown.elements["moves"]["selected"] == "2" && mount.shown.elements["moves"]["caption"] == "hud.move.break", "a drag picks a sector and names the move");
    bench.Press("strike", "commit", "break");
    ExpectEqual(mount.Last(), "combat:2\t6\t17\t4\tbreak\ttap", "the release commits the move");
    Expect(!mount.Has("moves") && mount.shown.dimmed.empty(), "the wheel closes");
    bench.Press("strike", "open", "fast");
    bench.Press("strike", "cancel");
    Expect(!mount.Has("moves") && mount.Last() == "combat:2\t6\t17\t4\tbreak\ttap", "a cancelled gesture commits nothing");

    // a move the engine does not offer is drawn dim and cannot be committed
    Json fewer = State("combat-melee");
    fewer["actions"] = Json{{"weapon", 11}, {"fast", 16}, {"force", 15}, {"block", 22}};
    bench.Set("combat", fewer);
    bench.Press("strike", "open", "fast");
    Expect(mount.shown.elements["moves"]["moves"][0]["disabled"] == true && mount.shown.elements["moves"]["moves"][4]["disabled"] == false, "unavailable moves are marked");
    const size_t sent = mount.commands.size();
    bench.Press("strike", "commit", "feint");
    Expect(mount.commands.size() == sent, "an unavailable move is not sent and nothing else is sent instead");
    Expect(!mount.Has("shot") && !mount.Has("potion"), "no pistol and no potion action: no buttons");

    // reloading
    bench.Feed("combat", "combat-melee-reload");
    const Json &shot = mount.shown.elements["shot"];
    Expect(shot["disabled"] == true && shot["reloading"] == true && shot["fill"] == 40 && shot["fillTone"] == "reload", "reloading: the fill rises, no shot");
    const size_t reloading = mount.commands.size();
    bench.Press("shot");
    Expect(mount.commands.size() == reloading, "an unloaded pistol does not fire");
    Json holster = State("combat-melee");
    holster["pistol"] = Json{{"state", "no_weapon"}, {"progress", 0}};
    bench.Set("combat", holster);
    Expect(!mount.Has("shot"), "no pistol equipped: no shot button");

    // a new activity restarts the sequence
    Json next = State("combat-melee");
    next["activityGeneration"] = 9;
    bench.Set("combat", next);
    bench.Press("weapon");
    ExpectEqual(mount.Last(), "combat:2\t9\t11\t1\tweapon\ttap", "a new activity generation counts from one");
}

// The hero's status from the vitals channel: effects row, potion count, officers, dash.
static void CheckStatus()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    bench.Feed("vitals", "vitals-status");
    const Json states = mount.shown.elements["vitals"]["states"];
    Expect(states.size() == 2 && states[0]["icon"] == "skull" && states[0]["fillTone"] == "danger" && states[1]["icon"] == "potion" &&
               states[1]["label"] == "hud.effect.healing",
           "effects: one small icon each, in a fixed order");
    Expect(mount.shown.elements["potion"]["badge"] == "3" && mount.shown.elements["potion"]["fillTone"] == "active", "potion: count and the healing fill");
    Expect(!mount.Has("officers") && !mount.Has("dash"), "explore: no officers column, no dash");

    Json combat = State("combat-melee");
    combat["actions"]["dash"] = 23;
    bench.Feed("gameplay", "gameplay-melee");
    bench.Set("combat", combat);
    const Json officers = mount.shown.elements["officers"]["officers"];
    Expect(officers.size() == 2 && officers[0]["texture"] == "INTERFACES/PORTRAITS/128/face_205" && officers[0]["hp"] == 84 &&
               officers[1]["texture"] == "" && officers[1]["name"] == "Офицер Бета",
           "combat: the officers column with portraits and health");
    Expect(mount.shown.elements["dash"]["fill"] == 40 && mount.shown.elements["dash"]["fillTone"] == "cooldown" &&
               mount.shown.elements["dash"]["disabled"] == true,
           "dash cooling down: filled and not pressable");
    Expect(!mount.Has("opponent"), "no cabin captain: no captain bar");
    Json cabin = State("vitals-status");
    cabin["opponent"] = Json{{"name", "Клод Дюран"}, {"health", 0.62}};
    bench.Set("vitals", cabin);
    Expect(mount.shown.elements["opponent"]["title"] == "hud.captain Клод Дюран" && mount.shown.elements["opponent-hp"]["value"] == 62,
           "cabin fight: the enemy captain's name and health on top");
    cabin["opponent"] = Json{{"name", ""}, {"health", 0.5}};
    bench.Set("vitals", cabin);
    Expect(!mount.errors.empty(), "an opponent without a name is rejected aloud");
    mount.errors.clear();
    Json ready = State("vitals-status");
    ready["dash"] = 0;
    bench.Set("vitals", ready);
    Expect(mount.shown.elements["dash"]["disabled"] == false && !mount.shown.elements["dash"].contains("fill"), "dash ready");
    mount.commands.clear();
    bench.Press("dash");
    ExpectEqual(mount.Last(), "combat:2\t6\t23\t1\tdash\ttap", "the dash is a combat action");
    ready.erase("dash");
    bench.Set("vitals", ready);
    Expect(!mount.Has("dash"), "no dash outside fight mode");
}

static void CheckSheet()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    mount.commands.clear();

    bench.Feed("landCommands", "land-commands-root");
    Expect(mount.shown.sheet == "s-hud-sheet" && mount.shown.modal && mount.Has("vitals") && !mount.Has("move") && !mount.Has("hero"),
           "an open menu: the sheet over the bars, no controls");
    const Json &props = mount.shown.sheet_props;
    Expect(props["title"] == "hud.sheet.actions" && props["back"] == false && props["layout"] == "rows" && props["rows"].size() == 6,
           "the root stage: ✕ only, six rows, the cancel item is not a row");
    Expect(props["rows"][0]["id"] == "item-c1" && props["rows"][0]["icon"] == "compass" && props["rows"][0]["chevron"] == true &&
               props["rows"][2]["icon"] == "thought" && props["rows"][2]["chevron"] == false && props["rows"][3]["disabled"] == true,
           "rows: icon by command, › for stages, disabled as the engine says");
    bench.Set("vitals", Json{{"health", 0.5}, {"energy", 0.5}});
    bench.Frame();
    bench.Frame(0.1);
    bench.Frame(5.0);
    Expect(mount.commands.empty(), "the sheet says nothing to the engine to cover its menu: the engine never draws it under the HUD");
    Expect(bench.hud.Tick() > 5.0, "… and the sheet asks for no frames");

    // the engine reports no gameplay mode while its menu is open: the sheet stays, nothing is closed
    Json blocked = State("gameplay-explore");
    blocked["mode"] = "unknown";
    blocked["revision"] = 20;
    const size_t open = mount.commands.size();
    bench.Set("gameplay", blocked);
    Expect(mount.visible && mount.shown.modal && mount.commands.size() == open, "gameplay blocked by the open menu: the sheet stays as it is");
    bench.Feed("gameplay", "gameplay-explore");

    // a tap acts at once: select, then activate against the revision that reports the selection
    bench.Press("item-c3");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tselectItem\tc3", "a tap selects the row …");
    Expect(mount.shown.sheet_props["rows"][2]["pressed"] == true, "… which is drawn pressed");
    bench.Press("item-c1");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tselectItem\tc3", "another tap waits for the first");
    bench.Feed("landCommands", "land-commands-root-selected");
    bench.Frame();
    ExpectEqual(mount.Last(), "landcommands:1\t4\t12\t2\tactivateItem\tc3", "… and the selected revision is activated");
    bench.Press("item-c4");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t12\t2\tactivateItem\tc3", "a disabled row does nothing");

    // the next stage
    bench.Feed("landCommands", "land-commands-travel");
    Expect(mount.shown.sheet_props["layout"] == "tiles" && mount.shown.sheet_props["back"] == true && mount.shown.sheet_props["title"] == "Переход" &&
               mount.shown.sheet_props["rows"][0]["icon"] == "tavern" && mount.shown.sheet_props["rows"][5]["icon"] == "usurer" &&
               mount.shown.sheet_props["rows"][4]["icon"] == "compass" &&
               mount.shown.sheet_props["rows"][0]["pressed"] == false,
           "travel: tiles, ←, place icons by fast-travel slot (the travel icon outside the table), the engine's title; the old tap is over");
    bench.Frame();
    bench.Press("sheet-back");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t13\t2\tback\t", "← goes back a stage");
    Expect(bench.hud.Back() && mount.Last() == "landcommands:1\t4\t13\t2\tback\t" && mount.commands.size() >= 2, "the back key does the same in a nested stage");
    bench.Press("sheet", "close");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t13\t2\tclose\t", "a tap beside the sheet closes it");

    bench.Feed("landCommands", "land-commands-potions");
    const Json &rows = mount.shown.sheet_props["rows"];
    Expect(mount.shown.sheet_props["layout"] == "dense" && rows.size() == 4 && rows[0]["heading"] == "hud.sheet.potions" && !rows[1].contains("heading") &&
               rows[3]["heading"] == "hud.sheet.abilities" && rows[3]["cooldown"] == 60 && rows[3]["disabled"] == true && rows[0]["cooldown"] == 0,
           "potions and abilities under headings; a recovering ability shows what is left");

    // a tap that the engine never answers ends by itself
    bench.Frame();
    bench.Press("item-p1");
    Expect(mount.shown.sheet_props["rows"][0]["pressed"] == true, "pressed while waiting");
    bench.Frame(1.6);
    Expect(mount.shown.sheet_props["rows"][0]["pressed"] == false, "the wait ends after 1.5 s");

    // the root stage closes on the back key
    bench.Set("landCommands", Json(nullptr));
    bench.Feed("landCommands", "land-commands-root");
    bench.Frame();
    Expect(bench.hud.Back() && mount.Last() == "landcommands:1\t4\t11\t2\tclose\t", "the back key closes the root stage");
    bench.Press("sheet-close");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tclose\t", "✕ closes");

    // losing the screen: the menu is closed
    mount.commands.clear();
    bench.Feed("presentation", "presentation-background");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tclose\t", "in the background the menu is closed");
    const size_t closed = mount.commands.size();
    bench.Frame(0.3);
    Expect(mount.commands.size() == closed, "… and nothing more is said while not live");
    bench.Feed("presentation", "presentation-game");
    bench.Frame();

    // the menu closed while gameplay is still blocked: the scene has nothing to hold on to
    bench.Set("gameplay", blocked);
    bench.Set("landCommands", Json(nullptr));
    Expect(!mount.visible && mount.Last() == "presence:hidden", "no menu and no mode: the HUD goes and tells the platform host");
    bench.Feed("gameplay", "gameplay-explore");
    Expect(mount.visible && mount.Last() == "presence:shown", "explore again: the HUD is back and says so");

    // a menu the sheet cannot show is closed and reported: the engine never draws its own
    bench.Set("landCommands", Json(nullptr));
    mount.commands.clear();
    bench.Feed("landCommands", "land-commands-unknown-kind");
    bench.Frame();
    Expect(mount.shown.sheet.empty() && !mount.shown.modal && !mount.Has("move") && mount.commands.size() == 1 &&
               mount.Last() == "landcommands:1\t4\t15\t2\tclose\t" && !mount.errors.empty() &&
               mount.errors.back().find("not drawable here") != std::string::npos && !bench.hud.Back(),
           "an undrawable menu: closed once, and that is reported");
}

static void CheckTouches(const std::string &pack)
{
    Runtime run(pack);
    if (!run.host)
        return;
    Host &host = *run.host;
    Expect(!host.WantsTouch(400, 200) && !host.WantsBack(), "nothing shown: every touch is the game's");
    run.Explore();

    // The test layout (800 × 400): the move zone at 0…336 × 176…400 with the stick resting at
    // 40…140 × 270…370, inside the zone the actions button at 250…302 × 338…390 and the log
    // box at 150…250 × 220…250; weapon at 728…780 × 338…390, context at 716…780 × 256…320,
    // hero at 682…730 × 10…58, vitals at 10…210 × 10…50.
    Expect(host.WantsTouch(90, 320), "the stick is the UI's");
    Expect(host.WantsTouch(750, 360) && host.WantsTouch(740, 290) && host.WantsTouch(700, 30), "buttons are the UI's");
    Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(500, 380), "the free scene is the game's");
    Expect(!host.WantsTouch(100, 30) && !host.WantsTouch(550, 20), "the bars and the location line show, they do not take touches");
    Expect(host.WantsTouch(240, 320) && host.WantsTouch(20, 390) && host.WantsTouch(330, 180), "the whole move zone is the UI's, not just the stick");
    Expect(!host.WantsTouch(340, 320) && !host.WantsTouch(100, 170), "just beside the zone is the game's");

    // the floating stick: it appears under the thumb, wherever in the zone that lands
    const size_t before = run.commands.size();
    host.Touch(TouchPhase::Down, 1, 250, 300);
    run.Frame();
    Expect(run.commands.size() == before, "a thumb that only landed moves nobody");
    host.Touch(TouchPhase::Move, 1, 250, 260);
    Expect(run.Last() == "move:1\ttown:42\t3\t1\t1\t0\t1000", "pushed up from where it landed: straight forward, not towards the place of rest: " + run.Last());
    Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(340, 320), "a held stick leaves the scene to the camera finger");
    host.Touch(TouchPhase::Down, 2, 400, 200); // the game's finger: the UI was not asked to take it, and does not act on it
    host.Touch(TouchPhase::Up, 2, 400, 200);
    Expect(run.Last() == "move:1\ttown:42\t3\t1\t1\t0\t1000", "a second finger outside the zone changes nothing");
    host.Touch(TouchPhase::Up, 1, 250, 260);
    Expect(run.Last() == "move:1\ttown:42\t3\t2\t0\t0\t0", "letting go stops the hero: " + run.Last());
    run.Frame();
    host.Touch(TouchPhase::Down, 1, 60, 200); // another place, over nothing
    host.Touch(TouchPhase::Move, 1, 100, 200);
    Expect(run.Last() == "move:1\ttown:42\t3\t3\t1\t1000\t0", "the next thumb gets its own base: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 100, 200);
    run.Frame();
    host.Touch(TouchPhase::Down, 1, 200, 235); // on the log box, which only shows
    host.Touch(TouchPhase::Move, 1, 200, 275);
    Expect(run.Last() == "move:1\ttown:42\t3\t5\t1\t0\t-1000", "a box that only shows does not shield the zone: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 200, 275);
    run.Frame();
    const size_t moves = run.commands.size();
    host.Touch(TouchPhase::Down, 1, 276, 364); // the actions button lies inside the zone's rectangle
    host.Touch(TouchPhase::Move, 1, 280, 360);
    host.Touch(TouchPhase::Up, 1, 280, 360);
    Expect(run.commands.size() == moves + 1 && run.Last() == "landcommands:1\t4\t10\t2\topen\t", "a button inside the zone wins over the stick: " + run.Last());
    run.commands.clear();
    run.Frame();

    // two pointers at once: the stick held, a button tapped
    host.Touch(TouchPhase::Down, 1, 90, 320);
    host.Touch(TouchPhase::Move, 1, 90, 280);
    run.Frame();
    Expect(run.Last() == "move:1\ttown:42\t3\t7\t1\t0\t1000", "the stick at its place of rest moves the hero forward: " + run.Last());
    Expect(host.WantsTouch(750, 360) && !host.WantsTouch(400, 200), "a held stick changes nothing for the other pointers");
    host.Touch(TouchPhase::Down, 2, 750, 360);
    host.Touch(TouchPhase::Up, 2, 750, 360);
    Expect(run.Sent("combat:2\t5\t11\t1\tweapon\ttap"), "a second finger taps the weapon while the stick is held");
    const size_t held = run.commands.size();
    run.Frame(3.0);
    Expect(run.commands.size() == held && !run.Sent("move:1\ttown:42\t3\t8\t0\t0\t0"), "the stick stays held through a slow frame, without a heartbeat");

    // the platform takes the gestures away
    host.CancelInput();
    Expect(run.Sent("move:1\ttown:42\t3\t8\t0\t0\t0"), "cancelled input releases the stick");
    const size_t count = run.commands.size();
    host.Touch(TouchPhase::Move, 1, 90, 300);
    host.Touch(TouchPhase::Up, 1, 90, 300);
    Expect(run.commands.size() == count, "the cancelled pointer is over");

    // combat: a held block and a swiped strike, each with its own finger
    run.Feed("gameplay", "gameplay-melee");
    run.Feed("combat", "combat-melee");
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(672, 282) && host.WantsTouch(110, 132), "strike and block are the UI's");
    // block sits left, above the floating stick's zone: right under it the zone still starts the stick
    run.commands.clear();
    host.Touch(TouchPhase::Down, 4, 110, 200);
    host.Touch(TouchPhase::Move, 4, 110, 170);
    Expect(run.Last().rfind("move:1\t", 0) == 0, "the stick zone below the block is the stick's: " + run.Last());
    host.Touch(TouchPhase::Up, 4, 110, 170);
    run.Frame();
    run.commands.clear();
    host.Touch(TouchPhase::Down, 1, 110, 132);
    Expect(run.Last() == "combat:2\t6\t22\t1\tblock\tdown", "block goes down with the finger: " + run.Last());
    host.Touch(TouchPhase::Move, 1, 200, 60); // far beyond a tap's slop: still held
    host.Touch(TouchPhase::Down, 2, 672, 282);
    host.Touch(TouchPhase::Move, 2, 720, 290); // right of the start: the thrust sector
    run.Frame();
    host.Touch(TouchPhase::Up, 2, 720, 290);
    Expect(run.Last() == "combat:2\t6\t17\t2\tbreak\ttap", "a swipe from the strike picks the move: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 200, 60);
    Expect(run.Last() == "combat:2\t6\t22\t3\tblock\tup", "block is let go with its finger: " + run.Last());
    run.Frame();
    host.Touch(TouchPhase::Down, 2, 672, 282);
    host.Touch(TouchPhase::Up, 2, 672, 282);
    Expect(run.Last() == "combat:2\t6\t16\t4\tfast\ttap", "a plain tap on the strike is the fast attack: " + run.Last());

    // a stick held through the stance change keeps working (the element is not remounted)
    host.Touch(TouchPhase::Down, 3, 90, 320);
    host.Touch(TouchPhase::Move, 3, 130, 320);
    run.Feed("gameplay", "gameplay-explore");
    run.Feed("combat", "combat-explore");
    run.Frame(0.12);
    Expect(run.Last().rfind("move:1\ttown:42\t3\t", 0) == 0 && run.Last().find("\t1\t1000\t0") != std::string::npos, "the stick survives melee → explore: " + run.Last());
    host.Touch(TouchPhase::Up, 3, 130, 320);

    // an open sheet is modal: every touch and the back key are the UI's
    run.Feed("landCommands", "land-commands-root");
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(400, 200) && host.WantsTouch(5, 5) && host.WantsBack(), "an open sheet takes every touch and the back key");
    host.Touch(TouchPhase::Down, 1, 700, 20); // the first row
    host.Touch(TouchPhase::Up, 1, 700, 20);
    Expect(run.Last() == "landcommands:1\t4\t11\t2\tselectItem\tc1", "a tap on a row: " + run.Last());
    host.Touch(TouchPhase::Down, 1, 100, 300); // beside the rows
    host.Touch(TouchPhase::Up, 1, 100, 300);
    Expect(run.Last() == "landcommands:1\t4\t11\t2\tclose\t", "a tap beside the sheet closes it: " + run.Last());
    host.Back();
    Expect(run.Last() == "landcommands:1\t4\t11\t2\tclose\t", "the back key closes the root stage");
    run.Feed("landCommands", "land-commands-idle");
    Expect(host.SetState("landCommands", "null"), "the menu is gone");
    run.Frame();
    Expect(!host.WantsTouch(400, 200) && !host.WantsBack(), "the sheet closed: the scene is the game's again");

    // what covers the game, or a game in the background, leaves nothing to touch
    run.Feed("presentation", "presentation-menu");
    run.Frame();
    Expect(!host.WantsTouch(90, 320) && !host.WantsTouch(750, 360), "under a menu of the platform host the HUD is gone");
    run.Feed("presentation", "presentation-background");
    run.Frame();
    Expect(!host.WantsTouch(90, 320), "not live: drawn, but nothing is claimed");
    run.Feed("presentation", "presentation-game");
    run.Frame();
    Expect(host.WantsTouch(90, 320), "live again");

    // a mode without a scene (every gameplay mode has one now: land, sea, world map)
    run.Feed("gameplay", "gameplay-orders");
    run.Frame();
    Expect(!host.WantsTouch(90, 320) && HostOwnsHud("unknown") && !HostOwnsHud("worldmap") && !HostOwnsHud("explore") && !HostOwnsHud("melee"),
           "a mode without a scene takes the land HUD away");

    // safe area: the layer moves in, and the claimed places with it
    run.Feed("gameplay", "gameplay-explore");
    host.SetSafeArea(50, 0, 30, 10);
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(140, 310) && !host.WantsTouch(40, 380) && !host.WantsTouch(140, 395) && host.WantsTouch(720, 350) && !host.WantsTouch(775, 385),
           "the safe area shifts the elements and what the UI claims");

    // errors are loud
    Expect(!host.SetState("vitals", "{broken"), "malformed JSON is refused");
}

// One host per process (RmlUi state is process-global): a second one is refused aloud, and
// the slot is free again once the first is gone.
static void CheckOneHostPerProcess(const std::string &pack)
{
    {
        Runtime first(pack);
        Config config;
        config.files = first.files.get();
        config.backend = &first.backend;
        config.width = 800;
        config.height = 400;
        std::string error;
        Expect(Host::Create(config, &error) == nullptr && error.find("one host per process") != std::string::npos,
               "a second host in the same process is refused with an error");
    }
    Runtime again(pack);
    Expect(again.host != nullptr, "after the first host is gone a new one starts");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    const std::string pack = argc > 2 ? argv[2] : "tests/pack";
    CheckExplore();
    CheckMovement();
    CheckCombat();
    CheckStatus();
    CheckSheet();
    CheckTouches(pack);
    CheckOneHostPerProcess(pack);
    if (failures)
        std::fprintf(stderr, "%d land HUD check(s) failed\n", failures);
    else
        std::printf("land HUD checks passed\n");
    return failures ? 1 : 0;
}
