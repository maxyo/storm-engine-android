// The land HUD: exploring and fighting on foot, and the land action sheet.
//
// Layout scenes `land` (explore) and `land-combat` (melee) of hud-layout.json. Input goes
// back through the engine's existing entry points:
//   move:1\t<inputOwner>\t<movementGeneration>\t<sequence>\t<active>\t<x>\t<forward>   (±1000)
//   combat:1\t<gameplayRevision>\t<activityGeneration>\t<sequence>\t<action>\t<tap|down|up|cancel>
//   landcommands:1\t<sceneEpoch>\t<revision>\t<activityGeneration>\t<verb>\t<id>
//   context:<action name>          shortcut:<mainmenu|character>
#include "hud.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace storm::ui::hud
{

namespace
{

constexpr double kIdle = 3600.0;
constexpr double kMoveHeartbeat = 0.1;    // the engine drops a stick it has not heard from for 350 ms
constexpr double kCoverageHeartbeat = 0.25; // … and a coverage acknowledgement after 750 ms
constexpr double kChoiceSeconds = 1.5;    // a tapped row waits this long for the engine to select it

struct Named
{
    const char *key, *icon, *label;
};

// Sword moves: the wheel sectors clockwise from the top left, then the centre. The strike
// button's swipe reports these keys (hud-layout.json: data swipe-ids, swipe-centre).
const Named kMoves[] = {
    {"feint", "feint", "Финт"},
    {"round", "attack-round", "Круговой удар"},
    {"break", "attack-pierce", "Пробивающий удар"},
    {"parry", "parry", "Парирование"},
    {"force", "attack-heavy", "Тяжёлый удар"},
    {"fast", "attack-fast", "Быстрый удар"},
};

// The script's quick action (BattleInterface "Log_SetActiveAction") in the context slot.
const Named kContext[] = {
    {"reload", "door", "Войти"},      {"pick", "hand", "Взять"},          {"openbox", "chest", "Открыть"},
    {"entertosea", "boat", "В море"}, {"entertoship", "boat", "На корабль"}, {"action", "hand", "Действие"},
};

// Rows of the action sheet by the script command behind them.
const Named kCommands[] = {
    {"BI_FastReload", "compass", ""}, {"BI_OutDoor", "door", ""},     {"BI_ItemsUse", "potion", ""},   {"BI_DialogStart", "talk", ""},
    {"BI_ItemsChange", "exchange", ""}, {"BI_TakeItem", "hand", ""},  {"BI_PlaceItem", "put", ""},     {"BI_UseBox", "chest", ""},
    {"BI_DeadBox", "search", ""},     {"BI_TalkSelf", "thought", ""}, {"BI_SelfRepair", "repair", ""}, {"BI_Alchemy", "retort", ""},
    {"BI_MapAtlas", "map", ""},       {"BI_ActivateRush", "energy", ""}, {"BI_Exit_Deck", "boat", ""},
};

// Places of the travel stage by a word of their script source.
const Named kPlaces[] = {
    {"portoffice", "portoffice", ""}, {"tavern", "tavern", ""}, {"store", "store", ""},    {"shipyard", "shipyard", ""}, {"townhall", "residence", ""},
    {"residence", "residence", ""},   {"bank", "usurer", ""},   {"usurer", "usurer", ""},  {"church", "church", ""},     {"brothel", "lantern", ""},
    {"prison", "prison", ""},         {"fort", "fort", ""},     {"port", "port", ""},
};

std::string Lower(std::string text)
{
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

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
            view.elements["vitals"] = Json::object();
            view.elements["health"] = Json{{"value", std::lround(states.vitals.health * 100.0)}};
            view.elements["energy"] = Json{{"value", std::lround(states.vitals.energy * 100.0)}};
        }

        const LandCommands &commands = states.land_commands;
        if (commands.known && commands.active)
        {
            if (commands.complete)
            {
                BuildSheet(commands, view, input);
                return;
            }
            // The engine marked its menu as something only it can draw. It stays on the
            // game's own surface (no coverage is acknowledged); the HUD keeps out of it.
            hud_.Error("land commands: the engine's menu is not drawable here (stage " + commands.stage + "); the game's own menu stays");
            return;
        }
        choice_.clear();

        if (!states.world_info.empty())
        {
            std::string line = states.world_info;
            for (size_t at = line.find('\n'); at != std::string::npos; at = line.find('\n', at))
                line.replace(at, 1, " · ");
            view.elements["location"] = Json{{"text", line}};
        }
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
            view.elements["potion"] = Json{{"disabled", !ready}};
            input.On("potion", "click", [this](const Event &) { Strike("potion", "tap"); });
        }
        if (combat.Has("weapon"))
        {
            view.elements["weapon"] = Json{{"disabled", !ready}, {"label", melee ? "Убрать оружие" : "Достать оружие"}};
            input.On("weapon", "click", [this](const Event &) { Strike("weapon", "tap"); });
        }

        if (melee)
            BuildCombat(states, ready, view, input);
        else
            BuildContext(states, ready, view, input);
    }

    void Hidden(const States &states) override
    {
        Uncover(states);
    }

    double Tick(const States &states, double now) override
    {
        double next = kIdle;
        if (moving_)
        {
            if (!states.gameplay.analog_movement || !hud_.Interactive())
                moving_ = false; // the owner is gone: the engine has dropped the stick already
            else
            {
                if (now - moved_at_ >= kMoveHeartbeat)
                    Move(states.gameplay, true);
                next = std::min(next, std::max(0.0, moved_at_ + kMoveHeartbeat - now));
            }
        }
        if (!choice_.empty() && now >= choice_until_)
        {
            choice_.clear();
            hud_.Rebuild();
        }
        else if (!choice_.empty())
            next = std::min(next, choice_until_ - now);
        if (!covered_.empty())
            next = std::min(next, std::max(0.0, covered_at_ + kCoverageHeartbeat - now));
        return next;
    }

    void Drawn(const States &states) override
    {
        // The sheet is on the screen with exactly this menu revision: only now may the
        // engine stop drawing its own. Repeated while it stays, as the engine expects.
        const LandCommands &commands = states.land_commands;
        const bool covering = commands.known && commands.active && commands.complete && hud_.Interactive() && hud_.Shown().modal;
        if (!covering)
        {
            Uncover(states);
            return;
        }
        const std::string owner = Owner(commands);
        const double now = hud_.Now();
        if (owner == covered_ && now - covered_at_ < kCoverageHeartbeat)
            return;
        covered_ = owner;
        covered_at_ = now;
        hud_.Send("landcommands", owner + "\tack\t1", "landcommands-ack");
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
        Uncover(states);
        if (commands.known && commands.active)
            hud_.Send("landcommands", Owner(commands) + "\tclose\t");
        choice_.clear();
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

    static std::string Owner(const LandCommands &commands)
    {
        return "1\t" + std::to_string(commands.scene) + "\t" + std::to_string(commands.revision) + "\t" + std::to_string(commands.activity);
    }

    // --- context slot (explore) ---

    void BuildContext(const States &states, bool ready, View &view, Input &input)
    {
        if (!ready)
            return;
        if (states.combat.Has("interact"))
        {
            // The engine's own target query: somebody to talk to stands in front of the hero.
            const bool talk = states.combat.interact_kind == "dialog";
            view.elements["context"] = Json{{"icon", talk ? "talk" : "hand"}, {"label", talk ? "Говорить" : "Действие"}};
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
        view.elements["context"] = Json{{"icon", known->icon}, {"label", known->label}};
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
                caption = move.label;
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
        hud_.Send("combat", "1\t" + std::to_string(combat.gameplay_revision) + "\t" + std::to_string(combat.activity) + "\t" +
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
        moved_at_ = hud_.Now();
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
            const std::string source = Lower(item.source);
            for (const Named &place : kPlaces)
                if (source.find(place.key) != std::string::npos)
                    return place.icon;
            return "compass";
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
                row["heading"] = item.kind == "potion" ? "Зелья" : item.kind == "ability" ? "Способности" : "";
            }
            rows.push_back(std::move(row));
            input.On(element, "click", [this, id = item.id](const Event &) { Choose(id); });
        }
        view.sheet = "s-hud-sheet";
        view.modal = true;
        view.sheet_props = Json{{"title", commands.title.empty() ? "Действия" : commands.title},
                                {"back", !root && commands.can_back},
                                {"layout", travel ? "tiles" : root ? "rows" : "dense"},
                                {"rows", rows}};
        input.On("sheet-back", "click", [this](const Event &) { Commands("back", {}); });
        input.On("sheet-close", "click", [this](const Event &) { Commands("close", {}); });
        input.On("sheet", "close", [this](const Event &) { Commands("close", {}); });
        if (covered_ != Owner(commands) && announced_ != Owner(commands))
        {
            announced_ = Owner(commands);
            hud_.Send("landcommands", announced_ + "\tack\t2", "landcommands-ack"); // this revision is on its way to the screen
        }
    }

    // The sheet does not cover the engine's menu (any more): say so once.
    void Uncover(const States &states)
    {
        if (covered_.empty() && announced_.empty())
            return;
        covered_.clear();
        announced_.clear();
        const LandCommands &commands = states.land_commands;
        if (commands.known)
            hud_.Send("landcommands", Owner(commands) + "\tack\t0", "landcommands-ack");
    }

    Hud &hud_;
    // movement
    bool moving_ = false;
    int x_ = 0, y_ = 0;
    double moved_at_ = 0.0;
    uint64_t move_sequence_ = 0;
    // combat
    uint64_t combat_activity_ = 0, combat_sequence_ = 0;
    bool wheel_open_ = false;
    std::string move_;
    // action sheet
    std::string covered_; // menu owner whose coverage was acknowledged last
    double covered_at_ = 0.0;
    std::string choice_; // tapped row on its way through select → activate
    double choice_until_ = 0.0;
    uint64_t choice_revision_ = 0;
    bool choice_activated_ = false;
    std::string announced_; // menu owner told that its sheet is being drawn
};

} // namespace

std::unique_ptr<Scene> CreateLandScene(Hud &hud)
{
    return std::make_unique<LandScene>(hud);
}

} // namespace storm::ui::hud
