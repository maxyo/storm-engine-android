// The land HUD: exploring and fighting on foot, and the land action sheet.
//
// Layout scenes `land` (explore) and `land-combat` (melee) of hud-layout.json. Input goes
// back through the engine's existing entry points:
//   move:1\t<inputOwner>\t<movementGeneration>\t<sequence>\t<active>\t<x>\t<forward>   (±1000)
//   combat:2\t<activityGeneration>\t<actionGeneration>\t<sequence>\t<action>\t<tap|down|up|cancel>
//     (the action's generation from the combat state; 0 for "all\tcancel")
//   landcommands:1\t<sceneEpoch>\t<revision>\t<activityGeneration>\t<verb>\t<id>
//   context:<action name>          shortcut:<mainmenu|character|characterscreen>
#include "hud.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace storm::ui::hud
{

namespace
{

constexpr double kIdle = 3600.0;
constexpr double kChoiceSeconds = 1.5;    // a tapped row waits this long for the engine to select it

using screens::StringKey;

// `label` is a key of the pack's string table ("" = the engine supplies the text).
struct Named
{
    const char *key, *icon, *label;
};

// Sword moves: the wheel sectors clockwise from the top left, then the centre. The strike
// button's swipe reports these keys (hud-layout.json: data swipe-ids, swipe-centre).
const Named kMoves[] = {
    {"feint", "feint", StringKey("hud.move.feint")},
    {"round", "attack-round", StringKey("hud.move.round")},
    {"break", "attack-pierce", StringKey("hud.move.break")},
    {"parry", "parry", StringKey("hud.move.parry")},
    {"force", "attack-heavy", StringKey("hud.move.force")},
    {"fast", "attack-fast", StringKey("hud.move.fast")},
};

// The script's quick action (BattleInterface "Log_SetActiveAction") in the context slot.
const Named kContext[] = {
    {"reload", "door", StringKey("hud.context.enter")},      {"pick", "hand", StringKey("hud.context.take")},          {"openbox", "chest", StringKey("hud.context.open")},
    {"entertosea", "boat", StringKey("hud.context.toSea")}, {"entertoship", "boat", StringKey("hud.context.toShip")}, {"action", "hand", StringKey("hud.context.action")},
};

// Rows of the action sheet by the script command behind them.
const Named kCommands[] = {
    {"BI_FastReload", "compass", ""}, {"BI_OutDoor", "door", ""},     {"BI_ItemsUse", "potion", ""},   {"BI_DialogStart", "talk", ""},
    {"BI_ItemsChange", "exchange", ""}, {"BI_TakeItem", "hand", ""},  {"BI_PlaceItem", "put", ""},     {"BI_UseBox", "chest", ""},
    {"BI_DeadBox", "search", ""},     {"BI_TalkSelf", "thought", ""}, {"BI_SelfRepair", "repair", ""}, {"BI_Alchemy", "retort", ""},
    {"BI_MapAtlas", "map", ""},       {"BI_ActivateRush", "energy", ""}, {"BI_Exit_Deck", "boat", ""},
};

// Places of the travel stage. The engine names a place only by its slot in the game's
// fast-travel table ("fr_<n>"), the same in every town; the label carries the local name.
const Named kPlaces[] = {
    {"fr_1", "store", ""},  {"fr_2", "shipyard", ""}, {"fr_3", "tavern", ""},  {"fr_4", "residence", ""}, {"fr_5", "usurer", ""},
    {"fr_6", "church", ""}, {"fr_7", "lantern", ""},  {"fr_8", "portoffice", ""}, {"fr_9", "prison", ""},
};

class LandScene final : public Scene
{
  public:
    explicit LandScene(Hud &hud) : hud_(hud)
    {
    }

    void Build(const States &states, View &view, Input &input) override
    {
        const Gameplay &gameplay = states.gameplay;
        const Combat &combat = states.combat;
        const bool melee = gameplay.mode == "melee";
        const bool ready = Ready(states);
        view.layout = melee ? "land-combat" : "land";

        if (states.vitals.known)
        {
            // The effects row under the bars (V2-Land-Explore: small state icons in one row).
            static const struct
            {
                const char *effect, *icon, *tone, *label;
            } kEffects[] = {{"poison", "skull", "danger", StringKey("hud.effect.poison")},
                            {"drunk", "rum", "effect", StringKey("hud.effect.drunk")},
                            {"healing", "potion", "active", StringKey("hud.effect.healing")}};
            Json effects = Json::array();
            for (const auto &known : kEffects)
                if (std::find(states.vitals.effects.begin(), states.vitals.effects.end(), known.effect) != states.vitals.effects.end())
                    effects.push_back(Json{{"icon", known.icon}, {"fill", 100}, {"fillTone", known.tone}, {"label", hud_.Text(known.label)}});
            if (!effects.empty())
                view.elements["effects"] = Json{{"states", effects}};
            BuildParty(states, melee, view, input);
        }

        const LandCommands &commands = states.land_commands;
        if (commands.known && commands.active)
        {
            if (commands.complete)
            {
                BuildSheet(commands, view, input);
                return;
            }
            // The engine marked its menu as something the sheet cannot show. The engine never
            // draws its own menu under the touch HUD, so an invisible menu would hold the
            // player: close it and report it.
            hud_.Error("land commands: the engine's menu is not drawable here (stage " + commands.stage + "); closed");
            hud_.Send("landcommands", Owner(commands) + "\tclose\t");
            return;
        }
        choice_.clear();

        const std::string place = Place(states.world_info);
        if (!place.empty())
            view.elements["location"] = Json{{"text", place}};
        view.log = true;
        const Json lines = hud_.LogLines();
        if (!lines.empty())
            view.elements["log"] = Json{{"lines", lines}};

        view.elements["hero"] = Json{{"disabled", melee}};
        input.On("hero", "click", [this](const Event &) { hud_.Send("shortcut", "character"); });
        view.elements["pause"] = Json::object();
        input.On("pause", "click", [this](const Event &) { hud_.Send("shortcut", "mainmenu"); });

        if (gameplay.analog_movement)
        {
            view.elements["move"] = Json::object();
            input.On("move", "change", [this](const Event &event) { Stick(event.value); });
        }
        else
            hud_.Error("land HUD: the engine offers no analog movement (inputOwner or movementGeneration missing); no move stick");

        if (commands.known && commands.available)
        {
            view.elements["actions"] = Json::object();
            input.On("actions", "click", [this](const Event &) { Commands("open", {}); });
        }
        if (combat.Has("potion"))
        {
            Json potion = {{"disabled", !ready}};
            if (states.vitals.known)
            {
                potion["badge"] = std::to_string(states.vitals.potions);
                // A potion still healing fills the button (V2-Land-Combat).
                if (std::find(states.vitals.effects.begin(), states.vitals.effects.end(), "healing") != states.vitals.effects.end())
                    potion["fill"] = 100, potion["fillTone"] = "active";
            }
            view.elements["potion"] = std::move(potion);
            input.On("potion", "click", [this](const Event &) { Strike("potion", "tap"); });
        }
        if (combat.Has("weapon"))
        {
            view.elements["weapon"] = Json{{"disabled", !ready}, {"label", melee ? hud_.Text("hud.weapon.sheathe") : hud_.Text("hud.weapon.draw")}};
            input.On("weapon", "click", [this](const Event &) { Strike("weapon", "tap"); });
        }

        if (melee)
            BuildCombat(states, ready, view, input);
        else
            BuildContext(states, ready, view, input);
    }

    double Tick(const States &states, double now) override
    {
        double next = kIdle;
        if (moving_)
        {
            if (!states.gameplay.analog_movement || !hud_.Interactive())
                moving_ = false; // the owner is gone: the engine has dropped the stick already
            else if (states.gameplay.input_owner != move_owner_ || states.gameplay.movement_generation != move_generation_)
                Move(states.gameplay, true); // a new movement session (owner, generation): the held stick goes on in it
        }
        if (!choice_.empty() && now >= choice_until_)
        {
            choice_.clear();
            hud_.Rebuild();
        }
        else if (!choice_.empty())
            next = std::min(next, choice_until_ - now);
        return next;
    }

    void Drawn(const States &states) override
    {
        // A tapped row goes on once the sheet showing the menu's new revision is on the screen
        // (select, then activate against the revision that reports the selection). The engine
        // never draws its own menu under the touch HUD (android_hud_presenter.hpp).
        const LandCommands &commands = states.land_commands;
        if (commands.known && commands.active && commands.complete && hud_.Interactive() && hud_.Shown().modal)
            Follow(commands);
    }

    void Released(const States &states) override
    {
        if (moving_)
        {
            x_ = y_ = 0;
            Move(states.gameplay, false);
        }
        wheel_open_ = false;
        move_.clear();
        if (states.combat.known && states.combat.mode != "unknown")
            Fight(states.combat, "all", "cancel");
        hud_.Rebuild();
    }

    void Suspended(const States &states) override
    {
        const LandCommands &commands = states.land_commands;
        if (commands.known && commands.active)
            hud_.Send("landcommands", Owner(commands) + "\tclose\t");
        choice_.clear();
    }

    bool Holds(const States &states) const override
    {
        return states.land_commands.known && states.land_commands.active; // the open menu blocks gameplay
    }

    bool Back(const States &states) override
    {
        const LandCommands &commands = states.land_commands;
        if (!commands.known || !commands.active || !commands.complete)
            return false;
        Commands(commands.stage == "commands" || !commands.can_back ? "close" : "back", {});
        return true;
    }

  private:
    static bool Ready(const States &states)
    {
        return states.combat.known && states.combat.gameplay_revision == states.gameplay.revision && states.combat.mode == states.gameplay.mode;
    }

    // The location line names the place only: town · place. The engine's text lists island,
    // town and place and then the date and the time, one per line, without saying which is
    // which; the calendar lines are the ones with digits, and the last two names are the
    // most local ones.
    static std::string Place(const std::string &lines)
    {
        std::vector<std::string> names;
        for (size_t at = 0; at <= lines.size();)
        {
            size_t end = lines.find('\n', at);
            if (end == std::string::npos)
                end = lines.size();
            const std::string line = lines.substr(at, end - at);
            if (!line.empty() && line.find_first_of("0123456789") == std::string::npos)
                names.push_back(line);
            at = end + 1;
        }
        if (names.empty())
            return {};
        return names.size() == 1 ? names.back() : names[names.size() - 2] + " · " + names.back();
    }

    static std::string Owner(const LandCommands &commands)
    {
        return "1\t" + std::to_string(commands.scene) + "\t" + std::to_string(commands.revision) + "\t" + std::to_string(commands.activity);
    }

    // --- the party ---

    static Picture Face(const std::string &face)
    {
        return {face.empty() ? std::string() : "INTERFACES/PORTRAITS/128/face_" + face, {}};
    }

    // The party row (as the original land interface's icons): the hero first, then the officers
    // who walk with him, each the face portrait with health and energy inside it. A tap opens
    // the character screen on that person; the game refuses that screen in a fight, so a tap
    // there does nothing.
    void BuildParty(const States &states, bool melee, View &view, Input &input)
    {
        const Vitals &vitals = states.vitals;
        const bool tap = !melee && vitals.hero >= 0;
        Json members = Json::array();
        members.push_back(MemberCell(vitals.name, Face(vitals.face), {{"health", vitals.health}, {"energy", vitals.energy}}, tap));
        for (const Vitals::Officer &officer : vitals.officers)
            members.push_back(MemberCell(officer.name, Face(officer.face), {{"health", officer.health}, {"energy", officer.energy}}, tap));
        view.elements["party"] = Json{{"officers", members}};
        if (!tap)
            return;
        input.On("party", "member", [this](const Event &event) {
            const Vitals &now = hud_.Current().vitals;
            const size_t index = static_cast<size_t>(std::max(0, std::atoi(event.value.c_str())));
            if (!now.known || index > now.officers.size())
                return;
            hud_.RequestSubject({"character", index == 0 ? now.hero : now.officers[index - 1].id});
            hud_.Send("shortcut", "characterscreen");
        });
    }

    // --- context slot (explore) ---

    void BuildContext(const States &states, bool ready, View &view, Input &input)
    {
        if (!ready)
            return;
        if (states.combat.Has("interact"))
        {
            // The engine's action key: somebody to talk to stands in front of the hero, or the game
            // waits for it to go on after a won boarding fight (it outranks the script's icon).
            const std::string &kind = states.combat.interact_kind;
            const bool talk = kind == "dialog", boarding = kind == "boarding";
            view.elements["context"] = Json{{"icon", talk ? "talk" : boarding ? "grapple" : "hand"},
                                            {"label", hud_.Text(talk ? "hud.context.talk" : boarding ? "hud.context.boarding" : "hud.context.action")}};
            input.On("context", "click", [this](const Event &) { Strike("interact", "tap"); });
            return;
        }
        const std::string action = Lower(states.context_action);
        // "Talk" belongs to the engine's target query above, never to the cached script icon.
        if (action.empty() || action == "talk" || action == "nothing")
            return;
        const Named *known = nullptr;
        for (const Named &entry : kContext)
            if (action == entry.key)
                known = &entry;
        if (!known)
        {
            // Documented fallback: an action name this table lacks still gets its button,
            // with the generic look, because the engine accepts any name it published.
            hud_.Log("land HUD: context action '" + states.context_action + "' has no icon of its own");
            known = &kContext[std::size(kContext) - 1];
        }
        view.elements["context"] = Json{{"icon", known->icon}, {"label", hud_.Text(known->label)}};
        input.On("context", "click", [this, name = states.context_action](const Event &) { hud_.Send("context", name); });
    }

    // --- combat set ---

    void BuildCombat(const States &states, bool ready, View &view, Input &input)
    {
        const Combat &combat = states.combat;
        Json moves = Json::array();
        bool any = false;
        std::string chosen = "centre", caption;
        for (size_t i = 0; i < std::size(kMoves); ++i)
        {
            const Named &move = kMoves[i];
            const bool offered = combat.Has(move.key);
            any = any || offered;
            // The last move sits in the centre of the wheel, the others in its sectors.
            const std::string sector = i + 1 == std::size(kMoves) ? "centre" : std::to_string(i);
            moves.push_back(Json{{"id", move.key}, {"icon", move.icon}, {"sector", sector}, {"disabled", !offered}});
            if (move_ == move.key)
            {
                chosen = sector;
                caption = hud_.Text(move.label);
            }
        }
        if (any)
        {
            view.elements["strike"] = Json{{"disabled", !ready}};
            input.On("strike", "open", [this](const Event &event) { Wheel(true, event.value); });
            input.On("strike", "preview", [this](const Event &event) { Wheel(true, event.value); });
            input.On("strike", "cancel", [this](const Event &) { Wheel(false, {}); });
            input.On("strike", "commit", [this](const Event &event) {
                Wheel(false, {});
                Strike(event.value, "tap");
            });
            input.Ignore("strike", "click");
            if (wheel_open_ && ready)
            {
                view.elements["moves"] = Json{{"moves", moves}, {"selected", chosen}, {"caption", caption}};
                view.dimmed = {"strike", "block", "shot", "dash", "potion", "weapon"};
            }
        }
        if (combat.Has("block"))
        {
            view.elements["block"] = Json{{"disabled", !ready}};
            input.On("block", "down", [this](const Event &) { Strike("block", "down"); });
            input.On("block", "up", [this](const Event &) { Strike("block", "up"); });
            input.Ignore("block", "click");
        }
        if (combat.Has("pistol") && combat.pistol != "no_weapon")
        {
            const bool loading = combat.pistol == "unloaded";
            Json shot = {{"disabled", !ready || loading}, {"reloading", loading}};
            if (loading)
            {
                shot["fill"] = combat.pistol_progress * 5;
                shot["fillTone"] = "reload";
            }
            if (states.vitals.known && states.vitals.capacity > 0)
                shot["badge"] = std::to_string(states.vitals.loaded);
            view.elements["shot"] = std::move(shot);
            input.On("shot", "click", [this](const Event &) { Strike("pistol", "tap"); });
        }
        if (combat.Has("dash") && states.vitals.known && states.vitals.dash >= 0.0)
        {
            const bool cooling = states.vitals.dash > 0.0;
            Json dash = {{"disabled", !ready || cooling}};
            if (cooling)
                dash["fill"] = std::lround(states.vitals.dash * 100.0), dash["fillTone"] = "cooldown";
            view.elements["dash"] = std::move(dash);
            input.On("dash", "click", [this](const Event &) { Strike("dash", "tap"); });
        }
        // The enemy captain of a boarding cabin fight (V2-Land-Boarding-Cabin): name and health.
        if (states.vitals.known && !states.vitals.opponent.empty())
        {
            view.elements["opponent"] = Json{{"title", hud_.Text("hud.captain") + " " + states.vitals.opponent}};
            view.elements["opponent-hp"] = Json{{"value", std::lround(states.vitals.opponent_health * 100.0)}};
        }
        // Elements of a dimmed cluster that are not shown now cannot be dimmed.
        view.dimmed.erase(std::remove_if(view.dimmed.begin(), view.dimmed.end(), [&view](const std::string &id) { return !view.elements.contains(id); }),
                          view.dimmed.end());
    }

    void Wheel(bool open, const std::string &move)
    {
        if (wheel_open_ == open && move_ == move)
            return;
        wheel_open_ = open;
        move_ = move;
        hud_.Rebuild();
    }

    // Sends a combat action when the engine still offers it to the player the HUD shows.
    void Strike(const std::string &action, const std::string &phase)
    {
        const States &states = hud_.Current();
        const Combat &combat = states.combat;
        if (!hud_.Interactive() || !Ready(states) || !combat.Has(action))
            return;
        if (action == "pistol" && combat.pistol != "unknown" && combat.pistol != "loaded")
            return;
        Fight(combat, action, phase);
    }

    void Fight(const Combat &combat, const std::string &action, const std::string &phase)
    {
        if (combat.activity != combat_activity_)
        {
            combat_activity_ = combat.activity;
            combat_sequence_ = 0;
        }
        const auto offered = combat.actions.find(action);
        const uint64_t generation = offered == combat.actions.end() ? 0 : offered->second;
        hud_.Send("combat", "2\t" + std::to_string(combat.activity) + "\t" + std::to_string(generation) + "\t" +
                                std::to_string(++combat_sequence_) + "\t" + action + "\t" + phase);
    }

    // --- movement ---

    void Stick(const std::string &value)
    {
        const size_t comma = value.find(',');
        if (comma == std::string::npos)
        {
            hud_.Error("land HUD: the move stick reported '" + value + "', expected 'x,y'");
            return;
        }
        // The stick reports ±100 with y growing downwards; the engine takes ±1000, forward up.
        x_ = static_cast<int>(std::clamp(std::strtod(value.c_str(), nullptr), -100.0, 100.0) * 10.0);
        y_ = static_cast<int>(std::clamp(std::strtod(value.c_str() + comma + 1, nullptr), -100.0, 100.0) * -10.0);
        const States &states = hud_.Current();
        if (!states.gameplay.analog_movement || !hud_.Interactive())
            return;
        Move(states.gameplay, x_ != 0 || y_ != 0);
    }

    void Move(const Gameplay &gameplay, bool active)
    {
        if (!active && !moving_)
            return;
        moving_ = active;
        move_owner_ = gameplay.input_owner;
        move_generation_ = gameplay.movement_generation;
        hud_.Send("move",
                  "1\t" + gameplay.input_owner + "\t" + std::to_string(gameplay.movement_generation) + "\t" + std::to_string(++move_sequence_) + "\t" +
                      (active ? "1" : "0") + "\t" + std::to_string(active ? x_ : 0) + "\t" + std::to_string(active ? y_ : 0),
                  "move");
    }

    // --- action sheet ---

    void Commands(const std::string &verb, const std::string &id)
    {
        const LandCommands &commands = hud_.Current().land_commands;
        if (!hud_.Interactive() || !commands.known)
            return;
        if (verb == "open" ? !commands.available || commands.active : !commands.active)
            return;
        if ((verb == "back" && !commands.can_back) || (verb == "close" && !commands.can_close))
            return;
        hud_.Send("landcommands", Owner(commands) + "\t" + verb + "\t" + id);
    }

    // A tap acts at once. The engine wants the row selected first and activated second,
    // each against the menu revision it answers to: the second step follows the revision
    // that reports the row as selected.
    void Choose(const std::string &id)
    {
        const LandCommands &commands = hud_.Current().land_commands;
        const LandCommands::Item *item = commands.Find(id);
        if (!item || !item->enabled || !choice_.empty())
            return;
        choice_ = id;
        choice_until_ = hud_.Now() + kChoiceSeconds;
        choice_revision_ = 0;
        choice_activated_ = false;
        hud_.Rebuild();
        Follow(commands);
    }

    void Follow(const LandCommands &commands)
    {
        if (choice_.empty() || choice_revision_ == commands.revision)
            return;
        const LandCommands::Item *item = commands.Find(choice_);
        if (!item || !item->enabled || choice_activated_)
        {
            // Activated (the menu moved on) or gone: the tap is over.
            choice_.clear();
            hud_.Rebuild();
            return;
        }
        choice_revision_ = commands.revision;
        choice_activated_ = commands.selected == choice_;
        Commands(choice_activated_ ? "activateItem" : "selectItem", choice_);
    }

    static const char *RowIcon(const LandCommands::Item &item)
    {
        if (item.kind == "potion")
            return "potion";
        if (item.kind == "ability")
            return "energy";
        if (item.kind == "travel")
        {
            for (const Named &place : kPlaces)
                if (item.source == place.key)
                    return place.icon;
            return "compass"; // a slot outside the table: the travel icon
        }
        for (const Named &command : kCommands)
            if (item.source == command.key)
                return command.icon;
        return "actions";
    }

    void BuildSheet(const LandCommands &commands, View &view, Input &input)
    {
        const bool root = commands.stage == "commands";
        const bool travel = commands.stage == "travel";
        Json rows = Json::array();
        std::string group;
        int headings = 0;
        for (const LandCommands::Item &item : commands.items)
        {
            if (item.kind == "cancel")
                continue;
            const std::string element = "item-" + item.id;
            Json row = {{"id", element},
                        {"title", item.label},
                        {"icon", RowIcon(item)},
                        {"disabled", !item.enabled},
                        {"pressed", item.id == choice_},
                        {"chevron", item.kind == "command" && (item.source == "BI_FastReload" || item.source == "BI_ItemsUse")},
                        {"cooldown", item.enabled ? 0 : std::lround((1.0 - item.cooldown) * 100.0)}};
            // Potions and abilities share one stage: each kind under its own heading.
            if (!root && !travel && item.kind != group)
            {
                group = item.kind;
                ++headings;
                row["heading"] = item.kind == "potion" ? hud_.Text("hud.sheet.potions") : hud_.Text("hud.sheet.abilities");
            }
            rows.push_back(std::move(row));
            input.On(element, "click", [this, id = item.id](const Event &) { Choose(id); });
        }
        view.sheet = "s-hud-sheet";
        view.modal = true;
        view.sheet_props = Json{{"title", commands.title.empty() ? hud_.Text("hud.sheet.actions") : commands.title},
                                {"back", !root && commands.can_back},
                                {"layout", travel ? "tiles" : root ? "rows" : "dense"},
                                {"headings", headings},
                                {"rows", rows}};
        input.On("sheet-back", "click", [this](const Event &) { Commands("back", {}); });
        input.On("sheet-close", "click", [this](const Event &) { Commands("close", {}); });
        input.On("sheet", "close", [this](const Event &) { Commands("close", {}); });
    }

    Hud &hud_;
    // movement
    bool moving_ = false;
    int x_ = 0, y_ = 0;
    // The stick is said on change only (event model): the engine holds it until its release.
    std::string move_owner_;
    uint64_t move_generation_ = 0;
    uint64_t move_sequence_ = 0;
    // combat
    uint64_t combat_activity_ = 0, combat_sequence_ = 0;
    bool wheel_open_ = false;
    std::string move_;
    // action sheet
    std::string choice_; // tapped row on its way through select → activate
    double choice_until_ = 0.0;
    uint64_t choice_revision_ = 0;
    bool choice_activated_ = false;
};

} // namespace

std::unique_ptr<Scene> CreateLandScene(Hud &hud)
{
    return std::make_unique<LandScene>(hud);
}

} // namespace storm::ui::hud
