// The spyglass overlay (mockups V2-Sea-Spyglass, V2-Sea-Spyglass-Fort): while the telescope
// is raised, the card of its target (ship or fort, its captain) lies over the HUD of any
// scene as the pack part <s-hud-spyglass>; the lens and the zoom stay the engine's. The Hud
// lays it (hud.cpp) and acknowledges each drawn frame, so the engine hides its own info.
//
// Channel "spyglass" (engine util/platform/android_spyglass.hpp):
//   {schemaVersion: 1, revision, on, generation, target: null | {kind: ship|fort, name, type,
//    class, hull, sails, crew, cannons, cannonsMax, charge 0…3, nation 0…4, sail, speed,
//    distance, captain, cannonType, portrait, skills: {fencing, cannons, accuracy, navigation,
//    boarding}}}
// A field the telescope cannot show at its level is left out.
#include "hud.h"

#include <cmath>
#include <cstdio>

namespace storm::ui::hud
{

namespace
{

constexpr const char *kSkills[] = {"fencing", "cannons", "accuracy", "navigation", "boarding"};
constexpr const char *kSkillIcons[] = {"sabre", "cannon", "accuracy", "wheel", "grapple"};
constexpr const char *kSkillKeys[] = {screens::StringKey("spy.skill_fencing"), screens::StringKey("spy.skill_cannons"),
                                      screens::StringKey("spy.skill_accuracy"), screens::StringKey("spy.skill_navigation"),
                                      screens::StringKey("spy.skill_boarding")};
// The game's nation order (ENGLAND, FRANCE, SPAIN, HOLLAND, PIRATE).
constexpr const char *kNations[] = {"england", "france", "spain", "holland", "pirates"};
constexpr const char *kNationKeys[] = {screens::StringKey("menu.nation_england"), screens::StringKey("menu.nation_france"),
                                       screens::StringKey("menu.nation_spain"), screens::StringKey("menu.nation_holland"),
                                       screens::StringKey("menu.nation_pirates")};
// The script's charge order (ispyglass.c: balls, bombs, chain, grape).
constexpr const char *kCharges[] = {screens::StringKey("spy.charge_balls"), screens::StringKey("spy.charge_bombs"),
                                    screens::StringKey("spy.charge_chain"), screens::StringKey("spy.charge_grape")};

// A whole number in [low, high], or -1 when absent; anything else is wrong.
bool Read(const Json &object, const char *key, int low, int high, int &out)
{
    out = -1;
    const auto found = object.find(key);
    if (found == object.end())
        return true;
    if (!found->is_number())
        return false;
    const double number = found->get<double>();
    if (!std::isfinite(number) || number != std::floor(number) || number < low || number > high)
        return false;
    out = static_cast<int>(number);
    return true;
}

bool ReadText(const Json &object, const char *key, std::string &out)
{
    out.clear();
    const auto found = object.find(key);
    if (found == object.end())
        return true;
    if (!found->is_string() || found->get<std::string>().size() > 256)
        return false;
    out = found->get<std::string>();
    return true;
}

bool ReadDecimal(const Json &object, const char *key, double &out)
{
    out = -1.0;
    const auto found = object.find(key);
    if (found == object.end())
        return true;
    if (!found->is_number() || !std::isfinite(found->get<double>()) || found->get<double>() < 0.0)
        return false;
    out = found->get<double>();
    return true;
}

// Fills `out` from the target object; returns what is wrong, or nothing.
std::string ReadTarget(const Json &t, Spyglass &out)
{
    if (!t.is_object())
        return "target is not an object";
    const auto kind = t.find("kind");
    if (kind == t.end() || !kind->is_string() || (*kind != "ship" && *kind != "fort"))
        return "target.kind must be ship or fort";
    out.kind = kind->get<std::string>();
    const bool texts = ReadText(t, "name", out.name) && ReadText(t, "type", out.type) && ReadText(t, "captain", out.captain) &&
                       ReadText(t, "cannonType", out.cannon_type) && ReadText(t, "portrait", out.portrait);
    const bool numbers = Read(t, "class", 1, 7, out.ship_class) && Read(t, "hull", 0, 100, out.hull) && Read(t, "sails", 0, 100, out.sails) &&
                         Read(t, "crew", 0, 100000, out.crew) && Read(t, "cannons", 0, 1000, out.cannons) &&
                         Read(t, "cannonsMax", 0, 1000, out.cannons_max) && Read(t, "charge", 0, 3, out.charge) &&
                         Read(t, "nation", 0, 4, out.nation) && Read(t, "sail", 0, 16, out.sail);
    const bool decimals = ReadDecimal(t, "speed", out.speed) && ReadDecimal(t, "distance", out.distance);
    if (!texts || !numbers || !decimals)
        return "a target field has the wrong type or range";
    const auto skills = t.find("skills");
    if (skills != t.end())
    {
        if (!skills->is_object())
            return "target.skills is not an object";
        for (int i = 0; i < 5; ++i)
            if (!Read(*skills, kSkills[i], 0, 100, out.skills[i]))
                return std::string("target.skills.") + kSkills[i] + " is not 0…100";
    }
    out.target = true;
    return {};
}

class SpyglassConsumer final : public screens::StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "spyglass";
    }
    void OnState(const Json &state, screens::Router &router) override
    {
        Hud *hud = router.Hud();
        if (!hud)
        {
            router.ReportError("state channel spyglass: this host has no HUD");
            return;
        }
        Spyglass next;
        std::string problem;
        const auto version = state.is_object() ? state.find("schemaVersion") : Json::const_iterator();
        const auto on = state.is_object() ? state.find("on") : Json::const_iterator();
        const auto generation = state.is_object() ? state.find("generation") : Json::const_iterator();
        if (!state.is_object() || version == state.end() || *version != 1 || on == state.end() || !on->is_boolean() ||
            generation == state.end() || !generation->is_number_unsigned())
            problem = "expected {schemaVersion: 1, on, generation, target}";
        else
        {
            next.on = on->get<bool>();
            next.generation = generation->get<uint64_t>();
            const auto target = state.find("target");
            if (target != state.end() && !target->is_null())
                problem = next.on ? ReadTarget(*target, next) : "a target while the telescope is down";
        }
        if (!problem.empty())
        {
            hud->Edit().spyglass = Spyglass(); // nothing half-read: the card goes, the engine draws its own
            hud->Reject(Channel(), problem);
            return;
        }
        next.known = true;
        hud->Edit().spyglass = next;
    }
};

std::string Decimal(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return buffer;
}

} // namespace

std::unique_ptr<screens::StateConsumer> CreateSpyglassConsumer()
{
    return std::make_unique<SpyglassConsumer>();
}

Json SpyglassProps(const Spyglass &s, Hud &hud)
{
    if (!s.target)
        return Json{{"empty", true}};
    const bool fort = s.kind == "fort";
    Json props{{"empty", false}, {"fort", fort}, {"icon", fort ? "fort" : "ship"}, {"name", s.name}};
    std::string sub;
    if (s.nation >= 0)
    {
        props["nation"] = std::string("nation-") + kNations[s.nation];
        sub = hud.Text(kNationKeys[s.nation]);
    }
    else
        props["nation"] = "";
    std::string type = fort ? hud.Text("spy.fort") : s.type;
    if (s.ship_class > 0)
        type += (type.empty() ? "" : ", ") + std::to_string(s.ship_class) + " " + hud.Text("yard.class");
    if (!type.empty())
        sub += (sub.empty() ? "" : " · ") + type;
    props["sub"] = sub;

    Json bars = Json::array();
    if (s.hull >= 0)
        bars.push_back(Json{{"icon", "hull"}, {"label", hud.Text("spy.hull")}, {"value", s.hull}, {"tone", "health"}});
    if (s.sails >= 0)
        bars.push_back(Json{{"icon", "sail-full"}, {"label", hud.Text("spy.sails")}, {"value", s.sails}, {"tone", "sails"}});
    props["bars"] = bars;

    Json facts = Json::array();
    if (s.cannons >= 0 && s.cannons_max >= 0)
        facts.push_back(Json{{"k", hud.Text("spy.cannons")}, {"v", std::to_string(s.cannons) + " " + hud.Text("common.of") + " " + std::to_string(s.cannons_max)}});
    if (s.speed >= 0.0)
        facts.push_back(Json{{"k", hud.Text("spy.speed")}, {"v", Decimal(s.speed) + " " + hud.Text("common.knots_short")}});
    if (s.distance >= 0.0)
        facts.push_back(Json{{"k", hud.Text("spy.distance")}, {"v", Decimal(s.distance)}});
    if (s.charge >= 0)
        facts.push_back(Json{{"k", hud.Text("spy.charge")}, {"v", hud.Text(kCharges[s.charge])}});
    if (s.crew >= 0)
        facts.push_back(Json{{"k", hud.Text("spy.crew")}, {"v", std::to_string(s.crew)}});
    props["facts"] = facts;

    Json skills = Json::array();
    for (int i = 0; i < 5; ++i)
        if (s.skills[i] >= 0)
            skills.push_back(Json{{"icon", kSkillIcons[i]}, {"label", hud.Text(kSkillKeys[i])}, {"value", std::to_string(s.skills[i])}});
    // A captain the telescope cannot make out: one line instead of the card.
    if (s.captain.empty())
    {
        props["captain"] = nullptr;
        props["unknown"] = hud.Text(fort ? "spy.no_commandant" : "spy.no_captain");
    }
    else
    {
        props["captain"] = Json{{"name", s.captain},
                                {"role", hud.Text(fort ? "spy.commandant" : "spy.captain")},
                                {"portrait", s.portrait},
                                {"skills", skills}};
        props["unknown"] = "";
    }
    return props;
}

} // namespace storm::ui::hud
