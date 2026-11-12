// Consumers of the HUD state channels: the engine's JSON is checked here, once, and kept
// as typed state for the scenes. Anything that does not fit its channel is rejected aloud
// and leaves that channel without data (its controls disappear), never half-read.
#include "hud.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace storm::ui::hud
{

namespace
{

using screens::Router;
using screens::StateConsumer;

constexpr double kMaxInteger = 9007199254740992.0; // 2^53: whole numbers a double keeps exactly

std::optional<uint64_t> Whole(const Json &object, const char *key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number())
        return std::nullopt;
    const double number = found->get<double>();
    if (!std::isfinite(number) || number < 0.0 || number > kMaxInteger || number != std::floor(number))
        return std::nullopt;
    return static_cast<uint64_t>(number);
}

std::optional<std::string> Text(const Json &object, const char *key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return std::nullopt;
    return found->get<std::string>();
}

std::optional<bool> Flag(const Json &object, const char *key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_boolean())
        return std::nullopt;
    return found->get<bool>();
}

// Identifiers the engine mints (players, items): never free text.
bool Token(const std::string &text, bool may_be_empty)
{
    if (text.empty())
        return may_be_empty;
    if (text.size() > 128)
        return false;
    for (const char c : text)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' || c == '-'))
            return false;
    return true;
}

bool OneOf(const std::string &text, std::initializer_list<const char *> options)
{
    for (const char *option : options)
        if (text == option)
            return true;
    return false;
}

// Text the engine shows the player: bounded, without control characters.
bool Printable(const std::string &text, size_t limit, bool may_be_blank)
{
    if (text.size() > limit)
        return false;
    bool blank = true;
    for (const unsigned char c : text)
    {
        if (c < 32 || c == 127)
            return false;
        blank = blank && c == ' ';
    }
    return may_be_blank || !blank;
}

// A finite number that is not negative; negative when the key is absent or something else.
double Amount(const Json &object, const char *key)
{
    const auto found = object.find(key);
    const double number = found != object.end() && found->is_number() ? found->get<double>() : -1.0;
    return std::isfinite(number) && number >= 0.0 ? number : -1.0;
}

double Ratio(const Json &object, const char *key)
{
    const auto found = object.find(key);
    const double number = found != object.end() && found->is_number() ? found->get<double>() : 0.0;
    return std::isfinite(number) ? std::clamp(number, 0.0, 1.0) : 0.0;
}

class HudConsumer : public StateConsumer
{
  public:
    void OnState(const Json &state, Router &router) final
    {
        Hud *hud = router.Hud();
        if (!hud)
        {
            router.ReportError(std::string("state channel ") + Channel() + ": this host has no HUD");
            return;
        }
        const std::string problem = Read(state, *hud);
        if (!problem.empty())
            hud->Reject(Channel(), problem);
    }

  protected:
    // Updates the HUD states; returns what is wrong with `state`, or nothing.
    virtual std::string Read(const Json &state, Hud &hud) = 0;
};

class GameplayConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "gameplay";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        Gameplay next;
        const auto revision = state.is_object() ? Whole(state, "revision") : std::nullopt;
        const auto mode = state.is_object() ? Text(state, "mode") : std::nullopt;
        if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1} || !revision || *revision == 0 || !mode)
        {
            hud.Edit().gameplay = next;
            return "expected {schemaVersion: 1, revision > 0, mode}";
        }
        next.revision = *revision;
        next.language = Text(state, "language").value_or("");
        // A mode this runtime does not know is no gameplay it can draw: the host keeps it.
        if (OneOf(*mode, {"explore", "melee", "sailing", "naval", "worldmap"}))
        {
            next.mode = *mode;
            const auto capabilities = state.find("capabilities");
            if (capabilities != state.end() && capabilities->is_array())
                for (const Json &name : *capabilities)
                    if (name.is_string() && next.capabilities.size() < 128)
                        next.capabilities.insert(name.get<std::string>());
        }
        std::string problem;
        if (next.mode != *mode && *mode != "unknown")
            problem = "unknown gameplay mode '" + *mode + "'";
        if (*mode == "unknown")
        {
            next.blocked = Text(state, "blocked").value_or("");
            if (!OneOf(next.blocked, {"starting", "paused", "loading", "interface", "dialogue", "cutscene", "video", "commands", "no-scene",
                                      "no-land-interface", "no-player", "group", "error"}))
                problem = "gameplay mode unknown without a known reason (blocked '" + next.blocked + "')";
        }
        if (next.mode == "explore" || next.mode == "melee")
        {
            next.input_owner = Text(state, "inputOwner").value_or("");
            const auto generation = Whole(state, "movementGeneration");
            next.analog_movement = Flag(state, "analogMovement").value_or(false) && !next.input_owner.empty() && generation && *generation > 0;
            next.movement_generation = next.analog_movement ? *generation : 0;
        }
        hud.Edit().gameplay = std::move(next);
        return problem;
    }
};

class CombatConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "combat";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        Combat next;
        const std::string problem = Parse(state, next);
        if (!problem.empty())
        {
            hud.Edit().combat = Combat();
            return problem;
        }
        // The engine restarts its activity generation with a new activity; within one,
        // an older gameplay revision arriving late is not news.
        const Combat &current = hud.Current().combat;
        if (current.known && (next.activity < current.activity || (next.activity == current.activity && next.gameplay_revision < current.gameplay_revision)))
            return {};
        hud.Edit().combat = std::move(next);
        return {};
    }

  private:
    static std::string Parse(const Json &state, Combat &out)
    {
        if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{2})
            return "expected {schemaVersion: 2, …}";
        const auto revision = Whole(state, "gameplayRevision"), activity = Whole(state, "activityGeneration");
        const auto player = Text(state, "playerId"), mode = Text(state, "mode");
        const auto actions = state.find("actions");
        if (!revision || !activity || !player || !mode || actions == state.end() || !actions->is_object())
            return "lacks gameplayRevision, activityGeneration, playerId, mode or actions";
        if (player->size() > 64 || !Token(*player, true) || !OneOf(*mode, {"unknown", "explore", "melee"}))
            return "bad playerId or mode";
        if (actions->size() > 12)
            return "too many actions";
        for (const auto &entry : actions->items())
        {
            const std::string &name = entry.key();
            if (!OneOf(name, {"weapon", "potion", "force", "fast", "break", "parry", "feint", "round", "pistol", "block", "freelook", "interact", "dash"}))
                return "unknown action '" + name + "'";
            const auto generation = Whole(*actions, name.c_str());
            if (!generation || *generation == 0)
                return "action '" + name + "' lacks its generation";
            out.actions[name] = *generation;
            if (*mode == "explore" && !OneOf(name, {"weapon", "potion", "freelook", "interact"}))
                return "action '" + name + "' does not belong to explore";
            if (*mode != "explore" && name == "interact")
                return "interact outside explore";
        }
        if (*mode != "unknown" ? (player->empty() || *activity == 0) : !out.actions.empty())
            return "mode and owner do not agree";
        const auto pistol = state.find("pistol");
        if (pistol != state.end())
        {
            const auto kind = pistol->is_object() ? Text(*pistol, "state") : std::nullopt;
            const auto progress = pistol->is_object() ? Whole(*pistol, "progress") : std::nullopt;
            if (!kind || !progress || !OneOf(*kind, {"unknown", "no_weapon", "unloaded", "loaded"}) || *progress > 20)
                return "bad pistol";
            if ((*kind == "loaded" && *progress != 20) || (*kind == "unloaded" && *progress >= 20) ||
                ((*kind == "unknown" || *kind == "no_weapon") && *progress != 0))
                return "pistol state and progress do not agree";
            out.pistol = *kind;
            out.pistol_progress = static_cast<int>(*progress);
        }
        out.known = true;
        out.gameplay_revision = *revision;
        out.activity = *activity;
        out.player = *player;
        out.mode = *mode;
        const std::string kind = Text(state, "interactKind").value_or("");
        out.interact_kind = kind == "dialog" || kind == "boarding" ? kind : "interact";
        return {};
    }
};

// null = the engine draws no land hero bars now.
class VitalsConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "vitals";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        Vitals next;
        if (state.is_object())
        {
            next.known = true;
            next.health = Ratio(state, "health");
            next.energy = Ratio(state, "energy");
            next.reload = Ratio(state, "reload");
            next.capacity = static_cast<int>(std::min<uint64_t>(Whole(state, "capacity").value_or(0), 100000));
            next.loaded = std::min(static_cast<int>(std::min<uint64_t>(Whole(state, "loaded").value_or(0), 100000)), next.capacity);
            if (std::string error = Status(state, next); !error.empty())
                return error;
        }
        hud.Edit().vitals = next;
        return state.is_object() || state.is_null() ? std::string() : "expected an object or null";
    }

  private:
    // The status fields; each one, when present, must have its shape.
    static std::string Status(const Json &state, Vitals &next)
    {
        static const std::set<std::string> kEffects = {"poison", "drunk", "healing"};
        if (const auto effects = state.find("effects"); effects != state.end())
        {
            if (!effects->is_array())
                return "effects: expected an array";
            for (const Json &effect : *effects)
            {
                if (!effect.is_string() || !kEffects.count(effect.get<std::string>()))
                    return "effects: unknown effect " + effect.dump();
                next.effects.push_back(effect.get<std::string>());
            }
        }
        if (state.contains("potions"))
        {
            const auto potions = Whole(state, "potions");
            if (!potions)
                return "potions: expected a whole number";
            next.potions = static_cast<int>(std::min<uint64_t>(*potions, 100000));
        }
        if (const auto officers = state.find("officers"); officers != state.end())
        {
            if (!officers->is_array())
                return "officers: expected an array";
            for (const Json &officer : *officers)
            {
                const auto id = officer.is_object() ? Whole(officer, "id") : std::nullopt;
                if (!id || !officer.contains("name") || !officer["name"].is_string() || !officer.contains("face") ||
                    !officer["face"].is_string() || !officer.contains("health") || !officer["health"].is_number())
                    return "officers: expected {id, name, face, health}";
                next.officers.push_back({static_cast<int>(*id), officer["name"].get<std::string>(), officer["face"].get<std::string>(),
                                         Ratio(officer, "health")});
            }
        }
        if (const auto dash = state.find("dash"); dash != state.end())
        {
            if (!dash->is_number())
                return "dash: expected a number";
            next.dash = Ratio(state, "dash");
        }
        if (const auto opponent = state.find("opponent"); opponent != state.end())
        {
            if (!opponent->is_object() || !opponent->contains("name") || !(*opponent)["name"].is_string() ||
                (*opponent)["name"].get<std::string>().empty() || !opponent->contains("health") || !(*opponent)["health"].is_number())
                return "opponent: expected {name, health}";
            next.opponent = (*opponent)["name"].get<std::string>();
            next.opponent_health = Ratio(*opponent, "health");
        }
        return {};
    }
};

// A JSON string: the lines the engine prints as the location text ("\n" between them).
class WorldInfoConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "worldInfo";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        hud.Edit().world_info = state.is_string() ? state.get<std::string>() : std::string();
        return state.is_string() ? std::string() : "expected a string";
    }
};

// A JSON string: the name of the script's quick action ("" = none).
class ContextActionConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "contextAction";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        const bool valid = state.is_string() && state.get<std::string>().size() <= 64;
        hud.Edit().context_action = valid ? state.get<std::string>() : std::string();
        return valid ? std::string() : "expected a short string";
    }
};

class LandCommandsConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "landCommands";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        if (state.is_null()) // the engine has no land menu now
        {
            hud.Edit().land_commands = LandCommands();
            return {};
        }
        LandCommands next;
        const std::string problem = Parse(state, next);
        if (!problem.empty())
        {
            hud.Edit().land_commands = LandCommands();
            return problem;
        }
        const LandCommands &current = hud.Current().land_commands;
        if (current.known && (next.activity < current.activity ||
                              (next.activity == current.activity &&
                               (next.scene < current.scene || (next.scene == current.scene && next.revision < current.revision)))))
            return {}; // late news of an older menu
        hud.Edit().land_commands = std::move(next);
        return {};
    }

  private:
    static std::string Parse(const Json &state, LandCommands &out)
    {
        if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1})
            return "expected {schemaVersion: 1, …}";
        const auto scene = Whole(state, "sceneEpoch"), revision = Whole(state, "revision"), activity = Whole(state, "activityGeneration");
        const auto active = Flag(state, "active"), available = Flag(state, "available"), back = Flag(state, "canBack"),
                   close = Flag(state, "canClose");
        const auto stage = Text(state, "stage"), selected = Text(state, "selectedItemId"), title = Text(state, "pendingLabel");
        const auto items = state.find("items");
        if (!scene || !revision || !activity || !active || !available || !back || !close || !stage || !selected || !title || !Whole(state, "mode") ||
            !Text(state, "language") || items == state.end() || !items->is_array())
            return "a required field is missing or mistyped";
        if (!OneOf(*stage, {"commands", "travel", "potions", "abilities"}))
            return "unknown stage '" + *stage + "'";
        if (!Token(*selected, true) || !Printable(*title, 1024, true) || items->size() > 256)
            return "bad selectedItemId, pendingLabel or item count";
        bool drawable = true;
        int cancels = 0;
        std::set<std::string> ids;
        for (const Json &source : *items)
        {
            LandCommands::Item item;
            const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
            const auto kind = source.is_object() ? Text(source, "kind") : std::nullopt;
            const auto label = source.is_object() ? Text(source, "label") : std::nullopt;
            const auto origin = source.is_object() ? Text(source, "source") : std::nullopt;
            const auto enabled = source.is_object() ? Flag(source, "enabled") : std::nullopt;
            const auto cooldown = source.is_object() ? source.find("cooldown") : Json::const_iterator();
            if (!id || !kind || !label || !origin || !enabled || cooldown == source.end() || !cooldown->is_number())
                return "an item lacks id, kind, label, source, enabled or cooldown";
            item.cooldown = cooldown->get<double>();
            if (!Token(*id, false) || !Token(*kind, false) || !Printable(*label, 1024, false) || !Printable(*origin, 512, true) ||
                !std::isfinite(item.cooldown) || item.cooldown < 0.0 || item.cooldown > 1.0)
                return "an item has a bad id, kind, label, source or cooldown";
            if (!ids.insert(*id).second)
                return "item id '" + *id + "' is listed twice";
            item.id = *id;
            item.kind = *kind;
            item.label = *label;
            item.source = *origin;
            item.enabled = *enabled;
            const bool cancel = item.kind == "cancel";
            cancels += cancel ? 1 : 0;
            // A kind this runtime does not know, or a command without its script source,
            // cannot be drawn truthfully: the menu is then left to the engine as a whole.
            drawable = drawable && OneOf(item.kind, {"command", "ability", "travel", "potion", "cancel"}) &&
                       (cancel || item.source.find_first_not_of(' ') != std::string::npos);
            out.items.push_back(std::move(item));
        }
        if (!selected->empty() && !ids.count(*selected))
            return "selectedItemId names no item";
        out.known = true;
        out.scene = *scene;
        out.revision = *revision;
        out.activity = *activity;
        out.active = *active;
        out.available = *available;
        out.can_back = *back;
        out.can_close = *close;
        out.stage = *stage;
        out.title = *title;
        out.selected = *selected;
        out.complete = drawable && Flag(state, "complete").value_or(false) && (!out.active || (out.can_back && out.can_close && cancels == 1));
        return {};
    }
};

// The player's ship at sea. `valid: false` = the engine has no sea scene to show.
class ShipConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "ship";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        Ship next;
        const std::string problem = Parse(state, next);
        if (!problem.empty())
        {
            hud.Edit().ship = Ship();
            return problem;
        }
        const Ship &current = hud.Current().ship;
        if (current.gameplay_revision == next.gameplay_revision && next.sequence < current.sequence)
            return {}; // late news of an older frame
        hud.Edit().ship = std::move(next);
        return {};
    }

  private:
    // current / maximum as a share; negative when the engine did not send the pair.
    static double Share(const Json &object, const char *key, const char *maximum)
    {
        const double value = Amount(object, key), limit = Amount(object, maximum);
        return value < 0.0 || limit <= 0.0 ? -1.0 : std::min(1.0, value / limit);
    }

    static std::string Vessel(const Json &object, Ship::Vessel &out)
    {
        const auto character = object.find("characterIndex");
        const auto entity = Text(object, "entityId");
        if (character == object.end() || !character->is_number_integer() || !entity || !Token(*entity, false))
            return "a ship lacks characterIndex or entityId";
        out.character = character->get<int>();
        out.entity = *entity;
        out.name = Text(object, "name").value_or("");
        out.type = Text(object, "type").value_or("");
        if (!Printable(out.name, 512, true) || !Printable(out.type, 128, true))
            return "a ship has a bad name or type";
        out.ship_class = static_cast<int>(std::min<uint64_t>(Whole(object, "shipClass").value_or(0), 9));
        out.hull = Share(object, "hull", "hullMax");
        out.crew = Share(object, "crew", "crewMax");
        out.sails = Share(object, "sailIntegrity", "sailIntegrityMax");
        return {};
    }

    static std::string Parse(const Json &state, Ship &out)
    {
        if (!state.is_object())
            return "expected an object, got " + std::string(state.type_name());
        if (Whole(state, "schemaVersion") != uint64_t{1})
            return "expected {schemaVersion: 1, …}";
        const auto revision = Whole(state, "gameplayRevision"), sequence = Whole(state, "sequence"), hud = Whole(state, "hudRevision");
        const auto valid = Flag(state, "valid");
        if (!revision || !sequence || !hud || !valid)
            return "lacks gameplayRevision, sequence, hudRevision or valid";
        out.gameplay_revision = *revision;
        out.sequence = *sequence;
        out.hud_revision = *hud;
        if (!*valid)
            return {};
        std::string problem = Vessel(state, out.own);
        if (!problem.empty())
            return problem;
        const auto fleet = state.find("fleet");
        if (fleet == state.end() || !fleet->is_array() || fleet->size() > 16)
            return "lacks the fleet list";
        for (const Json &source : *fleet)
        {
            Ship::Vessel vessel;
            problem = source.is_object() ? Vessel(source, vessel) : "a fleet entry is not an object";
            if (!problem.empty())
                return problem;
            out.fleet.push_back(std::move(vessel));
        }
        out.captions = Flag(state, "captionsVisible").value_or(false);
        out.navigator = Flag(state, "navigatorAvailable").value_or(false);
        out.zoom_in = Flag(state, "navigatorZoomIn").value_or(false);
        out.zoom_out = Flag(state, "navigatorZoomOut").value_or(false);
        out.navigator_generation = Whole(state, "navigatorGeneration").value_or(0);
        out.navigator_layout = Whole(state, "navigatorLayoutId").value_or(0);
        // The speed may be negative (a ship making sternway): any finite number.
        const auto speed = state.find("shipSpeedKnots");
        out.speed = speed != state.end() && speed->is_number() && std::isfinite(speed->get<double>()) ? std::abs(speed->get<double>()) : -1.0;
        out.wind = Amount(state, "windSpeed");
        out.cannon_charge = Amount(state, "cannonCharge") < 0.0 ? -1.0 : std::min(1.0, Amount(state, "cannonCharge"));
        out.time_scale = Amount(state, "timeScale");
        const auto abilities = state.find("abilities");
        if (abilities != state.end())
        {
            if (!abilities->is_array() || abilities->size() > 4)
                return "abilities is not a short list";
            for (const Json &source : *abilities)
            {
                Ship::Ability ability;
                ability.id = source.is_object() ? Text(source, "id").value_or("") : "";
                ability.state = source.is_object() ? Text(source, "state").value_or("") : "";
                ability.fill = source.is_object() ? Ratio(source, "fill") : 0.0;
                if (!OneOf(ability.id, {"InstantRepair", "ImmediateReload"}) || !OneOf(ability.state, {"ready", "active", "recovery", "blocked"}))
                    return "an unknown ability or ability state";
                out.abilities.push_back(std::move(ability));
            }
        }
        out.date_time = Text(state, "dateTimeText").value_or("");
        out.location = Text(state, "locationText").value_or("");
        if (!Printable(out.date_time, 160, true) || !Printable(out.location, 160, true))
            return "bad dateTimeText or locationText";
        out.ammo = Text(state, "ammo").value_or("");
        if (!out.ammo.empty() && !OneOf(out.ammo, {"balls", "grapes", "knippels", "bombs"}))
            return "unknown ammo '" + out.ammo + "'";
        out.ammo_count = Amount(state, "ammoCount");
        out.ammo_low = Flag(state, "navigatorAmmoWarning").value_or(false);
        out.powder_out = Flag(state, "navigatorPowderWarning").value_or(false);
        const auto counts = state.find("ammoCounts");
        if (counts != state.end() && counts->is_object())
        {
            const char *const kinds[] = {"balls", "grapes", "knippels", "bombs"};
            for (size_t i = 0; i < std::size(kinds); ++i)
                out.ammo_counts[i] = Amount(*counts, kinds[i]);
        }
        out.sail_state = Amount(state, "sailState") > 1.0 ? -1.0 : Amount(state, "sailState");
        out.can_set_sails = Flag(state, "canSetSails").value_or(false);
        out.known = true;
        return {};
    }
};

// null = the engine has no sea command menu now.
class SeaOrdersConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "seaOrders";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        if (state.is_null())
        {
            hud.Edit().sea_orders = SeaOrders();
            return {};
        }
        SeaOrders next;
        const std::string problem = Parse(state, next);
        if (!problem.empty())
        {
            hud.Edit().sea_orders = SeaOrders();
            return problem;
        }
        const SeaOrders &current = hud.Current().sea_orders;
        if (current.known && (next.activity < current.activity ||
                              (next.activity == current.activity &&
                               (next.scene < current.scene || (next.scene == current.scene && next.revision < current.revision)))))
            return {}; // late news of an older menu
        hud.Edit().sea_orders = std::move(next);
        return {};
    }

  private:
    static std::string Parse(const Json &state, SeaOrders &out)
    {
        if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1})
            return "expected {schemaVersion: 1, …}";
        const auto scene = Whole(state, "sceneEpoch"), revision = Whole(state, "revision"), activity = Whole(state, "activityGeneration");
        const auto active = Flag(state, "active"), available = Flag(state, "available"), back = Flag(state, "canBack"),
                   close = Flag(state, "canClose"), complete = Flag(state, "complete");
        const auto stage = Text(state, "stage"), selected = Text(state, "selectedItemId"), title = Text(state, "pendingLabel"),
                   recipient = Text(state, "recipientId");
        const auto items = state.find("items"), recipients = state.find("recipients");
        if (!scene || !revision || !activity || !active || !available || !back || !close || !complete || !stage || !selected || !title ||
            !recipient || items == state.end() || !items->is_array() || recipients == state.end() || !recipients->is_array())
            return "a required field is missing or mistyped";
        if (!OneOf(*stage, {"commands", "options", "targets"}))
            return "unknown stage '" + *stage + "'";
        if (!Token(*selected, true) || !Token(*recipient, true) || !Printable(*title, 1024, true) || items->size() > 256 || recipients->size() > 32)
            return "bad selectedItemId, recipientId, pendingLabel or list size";
        std::set<std::string> ids;
        for (const Json &source : *recipients)
        {
            SeaOrders::Recipient entry;
            const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
            const auto name = source.is_object() ? Text(source, "name") : std::nullopt;
            const auto entity = source.is_object() ? Text(source, "entityId") : std::nullopt;
            const auto main = source.is_object() ? Flag(source, "main") : std::nullopt;
            const auto character = source.is_object() ? source.find("characterIndex") : Json::const_iterator();
            if (!id || !name || !entity || !main || character == source.end() || !character->is_number_integer())
                return "a recipient lacks id, name, entityId, characterIndex or main";
            if (!Token(*id, false) || !Token(*entity, true) || !Printable(*name, 512, false))
                return "a recipient has a bad id, entityId or name";
            if (!ids.insert(*id).second)
                return "recipient id '" + *id + "' is listed twice";
            entry.id = *id;
            entry.name = *name;
            entry.entity = *entity;
            entry.character = character->get<int>();
            entry.main = *main;
            out.recipients.push_back(std::move(entry));
        }
        if (!recipient->empty() && !ids.count(*recipient))
            return "recipientId names no recipient";
        bool drawable = true;
        int cancels = 0;
        ids.clear();
        for (const Json &source : *items)
        {
            SeaOrders::Item item;
            const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
            const auto kind = source.is_object() ? Text(source, "kind") : std::nullopt;
            const auto label = source.is_object() ? Text(source, "label") : std::nullopt;
            const auto enabled = source.is_object() ? Flag(source, "enabled") : std::nullopt;
            const auto cooldown = source.is_object() ? source.find("cooldown") : Json::const_iterator();
            if (!id || !kind || !label || !enabled || cooldown == source.end() || !cooldown->is_number())
                return "an item lacks id, kind, label, enabled or cooldown";
            item.cooldown = cooldown->get<double>();
            item.source = Text(source, "source").value_or("");
            if (!Token(*id, false) || !Token(*kind, false) || !Printable(*label, 1024, false) || !Printable(item.source, 128, true) ||
                !std::isfinite(item.cooldown) || item.cooldown < 0.0 || item.cooldown > 1.0)
                return "an item has a bad id, kind, label, source or cooldown";
            if (!ids.insert(*id).second)
                return "item id '" + *id + "' is listed twice";
            item.id = *id;
            item.kind = *kind;
            item.label = *label;
            item.enabled = *enabled;
            cancels += item.kind == "cancel" ? 1 : 0;
            // A kind this runtime does not know cannot be drawn truthfully: the menu is then
            // left to the engine as a whole.
            drawable = drawable && OneOf(item.kind, {"command", "charge", "option", "ability", "ship", "fort", "land", "town", "cancel"});
            out.items.push_back(std::move(item));
        }
        if (!selected->empty() && !ids.count(*selected))
            return "selectedItemId names no item";
        if (*active && (recipient->empty() || out.recipients.empty()))
            return "an open menu without a recipient";
        out.known = true;
        out.scene = *scene;
        out.revision = *revision;
        out.activity = *activity;
        out.active = *active;
        out.available = *available;
        out.can_back = *back;
        out.can_close = *close;
        out.stage = *stage;
        out.recipient = *recipient;
        out.title = *title;
        out.selected = *selected;
        out.complete = drawable && *complete && cancels <= 1 && (!out.active || (out.can_back && out.can_close));
        return {};
    }
};

class SeaQuickConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "seaQuickAction";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        SeaQuick next;
        const auto activity = state.is_object() ? Whole(state, "activity") : std::nullopt;
        const auto revision = state.is_object() ? Whole(state, "revision") : std::nullopt;
        const auto available = state.is_object() ? Flag(state, "available") : std::nullopt;
        const auto action = state.is_object() ? Text(state, "action") : std::nullopt;
        const auto label = state.is_object() ? Text(state, "label") : std::nullopt;
        std::string problem;
        if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1} || !activity || !revision || !available || !action || !label)
            problem = "expected {schemaVersion: 1, activity, revision, available, action, label}";
        else if (*available && !OneOf(*action, {"Map", "Moor", "Board", "LandTroops", "Reload"}))
            problem = "unknown sea quick action '" + *action + "'";
        else if (*available && !Printable(*label, 1024, false))
            problem = "a quick action without its caption";
        if (!problem.empty())
        {
            hud.Edit().sea_quick = next;
            return problem;
        }
        const SeaQuick &current = hud.Current().sea_quick;
        if (current.known && (*activity < current.activity || (*activity == current.activity && *revision < current.revision)))
            return {}; // late news of an older offer
        next.known = true;
        next.activity = *activity;
        next.revision = *revision;
        next.available = *available;
        next.action = *available ? *action : std::string();
        next.label = *available ? *label : std::string();
        hud.Edit().sea_quick = std::move(next);
        return {};
    }
};

// The world map. A frame of an older revision of the same activity is not news.
class WorldMapConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "worldMap";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        WorldMap next;
        const std::string problem = Parse(state, next);
        if (!problem.empty())
        {
            hud.Edit().world_map = WorldMap();
            return problem;
        }
        const WorldMap &current = hud.Current().world_map;
        if (current.known && current.activity == next.activity && current.gameplay_revision == next.gameplay_revision && next.revision < current.revision)
            return {};
        hud.Edit().world_map = std::move(next);
        return {};
    }

  private:
    // A finite number in [low, high]; `fallback` when absent or null.
    static std::optional<double> Number(const Json &object, const char *key, double low, double high, bool &bad)
    {
        const auto found = object.find(key);
        if (found == object.end() || found->is_null())
            return std::nullopt;
        if (!found->is_number() || !std::isfinite(found->get<double>()) || found->get<double>() < low || found->get<double>() > high)
        {
            bad = true;
            return std::nullopt;
        }
        return found->get<double>();
    }

    static std::string Parse(const Json &state, WorldMap &out)
    {
        if (!state.is_object())
            return "expected an object, got " + std::string(state.type_name());
        if (Whole(state, "schemaVersion") != uint64_t{1})
            return "expected {schemaVersion: 1, …}";
        const auto revision = Whole(state, "revision"), activity = Whole(state, "activity"), gameplay = Whole(state, "gameplayRevision"),
                   mask = Whole(state, "mask");
        const auto available = Flag(state, "available"), ready = Flag(state, "commandsReady"), ships = Flag(state, "shipsVisible"),
                   instruments = Flag(state, "instrumentsVisible");
        const auto date = Text(state, "date"), coordinates = Text(state, "coordinates");
        const auto fleet = state.find("ships"), commands = state.find("commands");
        if (!revision || !activity || !gameplay || !mask || !available || !ready || !ships || !instruments || !date || !coordinates ||
            fleet == state.end() || !fleet->is_array() || commands == state.end() || !commands->is_array())
            return "a required field is missing or mistyped";
        if (*mask > 7 || fleet->size() > 8 || commands->size() > 6 || !Printable(*date, 160, true) || !Printable(*coordinates, 160, true))
            return "bad mask, list size, date or coordinates";
        bool bad = false;
        const auto hour = Number(state, "hour", 0.0, 24.0, bad), force = Number(state, "windForce", 0.0, 1.0, bad),
                   wind = Number(state, "windAngle", -7.0, 7.0, bad), heading = Number(state, "heading", -1e6, 1e6, bad),
                   morale = Number(state, "morale", 0.0, 100.0, bad), speed = Number(state, "speed", 0.0, 1.0, bad),
                   time_scale = Number(state, "timeScale", 0.0, 100.0, bad),
                   food = Number(state, "foodDays", 0.0, 1e9, bad), rum = Number(state, "rumDays", 0.0, 1e9, bad),
                   flag = Number(state, "flag", 0.0, 4.0, bad), storm = Number(state, "stormState", 0.0, 2.0, bad);
        if (bad)
            return "an instrument value is out of range";
        for (const Json &source : *fleet)
        {
            WorldMap::Vessel vessel;
            const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
            const auto name = source.is_object() ? Text(source, "name") : std::nullopt;
            if (!id || !name || !Printable(*id, 256, true) || !Printable(*name, 160, true))
                return "a ship lacks id or name";
            vessel.id = *id;
            vessel.name = *name;
            vessel.type = Text(source, "type").value_or("");
            vessel.hull = Number(source, "hull", 0.0, 100.0, bad).value_or(-1.0);
            if (bad || !Printable(vessel.type, 128, true))
                return "a ship has a bad hull or type";
            out.fleet.push_back(std::move(vessel));
        }
        std::set<std::string> ids;
        for (const Json &source : *commands)
        {
            const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
            const auto label = source.is_object() ? Text(source, "label") : std::nullopt;
            if (!id || !label || !OneOf(*id, {"EnterToSea", "EnterToIsland", "EnterToShip", "EnterToStorm", "EnterToAttack", "EnterToEnemy"}) ||
                !Printable(*label, 160, false))
                return "unknown or unlabelled command";
            if (!ids.insert(*id).second)
                return "command '" + *id + "' is listed twice";
            out.commands.push_back({*id, *label});
        }
        out.known = true;
        out.available = *available;
        out.revision = *revision;
        out.activity = *activity;
        out.gameplay_revision = *gameplay;
        out.mask = static_cast<unsigned>(*mask);
        out.instruments = *instruments;
        out.ships_visible = *ships;
        out.commands_ready = *ready;
        out.date = *date;
        out.coordinates = *coordinates;
        out.hour = hour.value_or(-1.0);
        out.wind_force = force.value_or(-1.0);
        out.morale = morale.value_or(-1.0);
        out.speed = speed.value_or(-1.0);
        out.time_scale = time_scale.value_or(-1.0);
        out.angles = wind && heading;
        out.wind_angle = wind.value_or(0.0);
        out.heading = heading.value_or(0.0);
        out.food_days = food ? static_cast<int>(*food) : -1;
        out.rum_days = rum ? static_cast<int>(*rum) : -1;
        out.flag = flag ? static_cast<int>(*flag) : -1;
        out.storm = storm ? static_cast<int>(*storm) : -1;
        return {};
    }
};

class PresentationConsumer final : public HudConsumer
{
  public:
    const char *Channel() const override
    {
        return "presentation";
    }

  protected:
    std::string Read(const Json &state, Hud &hud) override
    {
        Presentation next;
        const auto screen = state.is_object() ? Text(state, "screen") : std::nullopt;
        const auto loading = state.is_object() ? Flag(state, "loading") : std::nullopt;
        const auto cinematic = state.is_object() ? Flag(state, "cinematic") : std::nullopt;
        const auto dialogue = state.is_object() ? Flag(state, "dialogue") : std::nullopt;
        const auto live = state.is_object() ? Flag(state, "live") : std::nullopt;
        const bool valid = screen && loading && cinematic && dialogue && live;
        if (valid)
        {
            next.known = true;
            next.screen = *screen;
            next.loading = *loading;
            next.cinematic = *cinematic;
            next.dialogue = *dialogue;
            next.live = *live;
        }
        hud.Edit().presentation = next;
        return valid ? std::string() : "expected {screen, loading, cinematic, dialogue, live}";
    }
};

} // namespace

void AddStateConsumers(std::vector<std::unique_ptr<screens::StateConsumer>> &consumers)
{
    consumers.push_back(std::make_unique<GameplayConsumer>());
    consumers.push_back(std::make_unique<CombatConsumer>());
    consumers.push_back(std::make_unique<VitalsConsumer>());
    consumers.push_back(std::make_unique<WorldInfoConsumer>());
    consumers.push_back(std::make_unique<ContextActionConsumer>());
    consumers.push_back(std::make_unique<LandCommandsConsumer>());
    consumers.push_back(std::make_unique<ShipConsumer>());
    consumers.push_back(std::make_unique<SeaOrdersConsumer>());
    consumers.push_back(std::make_unique<SeaQuickConsumer>());
    consumers.push_back(std::make_unique<WorldMapConsumer>());
    consumers.push_back(std::make_unique<PresentationConsumer>());
    consumers.push_back(CreateSpyglassConsumer()); // spyglass.cpp
}

} // namespace storm::ui::hud
