#include "platform/android_gameplay.hpp"
#include "attributes.h"
#include <cassert>
#include <iostream>
#include <map>

class Codec final : public VSTRING_CODEC
{
    std::map<uint32_t, std::string> values;
public:
    uint32_t GetNum() override { return values.size(); }
    uint32_t Convert(const char *value) override { return Convert(value, std::char_traits<char>::length(value)); }
    uint32_t Convert(const char *value, int32_t length) override
    {
        const std::string text(value, length);
        for (const auto &entry : values) if (entry.second == text) return entry.first;
        const auto code = uint32_t(values.size() + 1);
        values[code] = text;
        return code;
    }
    const char *Convert(uint32_t code) override { return values.at(code).c_str(); }
    void VariableChanged() override {}
};

// Script/entity service boundaries; use the actual readGameplaySnapshot body
// and engine ATTRIBUTES, including its modal checks and capability selection.
struct VDATA
{
    int32_t integer = 0;
    std::string text;
    std::vector<ATTRIBUTES *> attributes;
    bool Get(int32_t &value) { value = integer; return true; }
    bool Get(const char *&value) { value = text.c_str(); return true; }
    uint32_t GetElementsNum() const { return attributes.size(); }
    ATTRIBUTES *GetAClass(int index = 0) { return index >= 0 && size_t(index) < attributes.size() ? attributes[index] : nullptr; }
};
struct VSTRSERVICE { const char *GetLanguage() const { return "russian"; } } strings;
constexpr uint64_t invalid_entity = 0;
constexpr int MSG_CHARACTER_EX_MSG = 1, SHIP_SAIL = 0, SHIP_WAR = 1;
struct Core
{
    std::map<std::string, VDATA> variables;
    std::map<std::string, uint64_t> entities{{"Player", 7}, {"location", 8}};
    ATTRIBUTES *playerAttributes = nullptr;
    bool fighting = false;
    void *GetScriptVariable(const char *name)
    {
        const auto found = variables.find(name);
        return found == variables.end() ? nullptr : &found->second;
    }
    uint64_t GetEntityId(const char *name) const
    {
        const auto found = entities.find(name);
        return found == entities.end() ? invalid_entity : found->second;
    }
    ATTRIBUTES *Entity_GetAttributePointer(uint64_t) { return playerAttributes; }
    uint64_t Send_Message(uint64_t, const char *, int, const char *) { return fighting; }
    void *GetService(const char *) { return &strings; }
} core;
struct Ship { uint64_t GetId() const { return 20; } } ship;
Ship *shipForCharacter(ATTRIBUTES *) { return &ship; }
namespace storm::android
{
#include "snapshot.inc"
}

int main()
{
    using namespace storm::android;
    Codec codec;
    ATTRIBUTES character(codec), otherCharacter(codec), interfaceState(codec), bindings(codec), land(codec), battle(codec);
    core.playerAttributes = &character;
    character.SetAttribute("location", "city");
    auto *ai = character.CreateAttribute("chr_ai", nullptr);
    ai->SetAttribute("type", "player");
    core.variables["Characters"].attributes = {&character};
    core.variables["nMainCharacterIndex"].integer = 0;
    core.variables["curKeyGroupName"].text = "PrimaryLand";
    core.variables["bLandInterfaceStart"].integer = 1;
    core.variables["InterfaceStates"].attributes = {&interfaceState};
    core.variables["objControlsState"].attributes = {&bindings};
    core.variables["objLandInterface"].attributes = {&land};
    core.variables["BattleInterface"].attributes = {&battle};
    auto *groups = bindings.CreateAttribute("keygroups", nullptr);
    for (const char *name : {"PrimaryLand", "FightModeControls"})
    {
        auto *group = groups->CreateAttribute(name, nullptr);
        for (const char *control : {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight", "ChrFightMode"})
            group->CreateAttribute(control, nullptr);
        group->CreateAttribute(std::string(name) == "PrimaryLand" ? "ChrAction" : "ChrAttackBase", nullptr);
    }
    const auto unknown = [&] {
        const auto snapshot = readGameplaySnapshot(true);
        assert(snapshot.mode == "unknown" && snapshot.inputOwner.empty() && snapshot.capabilities.empty());
    };
    auto start = readGameplaySnapshot(true);
    assert(start.mode == "explore" && !start.inputOwner.empty());
    GameplayState state;
    state.update(start);
    for (const char *group : {"PrimaryLand", "FightModeControls"})
        for (bool fight : {false, true})
        {
            core.variables["curKeyGroupName"].text = group;
            core.fighting = fight;
            const auto next = readGameplaySnapshot(true);
            assert(next.mode == (fight ? "melee" : "explore"));
            assert(next.inputOwner == start.inputOwner);
            for (const char *movement : {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight"})
                assert(std::find(next.capabilities.begin(), next.capabilities.end(), movement) != next.capabilities.end());
            // A stale PrimaryLand group must not invent fight-group bindings.
            if (std::string(group) == "PrimaryLand")
                assert(std::find(next.capabilities.begin(), next.capabilities.end(), "ChrAttackBase") == next.capabilities.end());
            state.update(next);
            assert(state.snapshot().inputOwner == start.inputOwner);
        }

    // Known stance never overrides the same hard guards that produced the
    // legitimate guard-dialogue interruption in the runtime regression.
    core.variables["dialogRun"].integer = 1; unknown();
    core.variables["dialogRun"].integer = 0;
    interfaceState.SetAttributeUseDword("Launched", 1); unknown();
    interfaceState.SetAttributeUseDword("Launched", 0);
    ai->SetAttribute("type", "actor"); unknown(); ai->SetAttribute("type", "player");
    for (auto *command : {&land, &battle})
    {
        command->SetAttributeUseDword("ComState", 1); unknown();
        command->SetAttributeUseDword("ComState", 0);
    }
    for (const char *entity : {"WorldMap", "CAviPlayer", "WMVideoPlay"})
    {
        core.entities[entity] = 10; unknown(); core.entities.erase(entity);
    }
    auto *mapBindings = groups->CreateAttribute("WorldMapControls", nullptr);
    for (const char *control : {"WMapShipSailUp", "WMapShipSailDown", "WMapShipTurnLeft", "WMapShipTurnRight", "WMapCancel", "WMapCameraSwitch"})
        mapBindings->CreateAttribute(control, nullptr);
    core.entities["WorldMap"] = 21;
    core.variables["curKeyGroupName"].text = "WorldMapControls";
    core.variables["bSeaActive"].integer = 1; // old sea flags must not mask the live map
    auto map = readGameplaySnapshot(true);
    assert(map.mode == "worldmap" && map.inputOwner.empty() && map.capabilities.size() == 6);
    assert(std::find(map.capabilities.begin(), map.capabilities.end(), "ChrAction") == map.capabilities.end());
    core.entities["WorldMap"] = 22;
    assert(readGameplaySnapshot(true).scene != map.scene);
    interfaceState.SetAttributeUseDword("Launched", 1); unknown();
    interfaceState.SetAttributeUseDword("Launched", 0);
    core.entities["CAviPlayer"] = 23; unknown(); core.entities.erase("CAviPlayer");
    core.entities.erase("WorldMap"); unknown(); // group alone cannot authorize map input
    core.variables["bSeaActive"].integer = 0;
    core.variables["curKeyGroupName"].text = "BattleInterfaceControls"; unknown();
    core.variables["curKeyGroupName"].text = "PrimaryLand";
    core.variables["bLandInterfaceStart"].integer = 0; unknown();
    core.variables["bLandInterfaceStart"].integer = 1;
    core.entities["Player"] = invalid_entity; unknown(); core.entities["Player"] = 7;
    core.playerAttributes = &otherCharacter; unknown(); core.playerAttributes = &character;
    const auto notReady = readGameplaySnapshot(false); // App background/loading gate.
    assert(notReady.mode == "unknown" && notReady.inputOwner.empty());

    core.fighting = false;
    core.entities["Player"] = 9;
    assert(readGameplaySnapshot(true).inputOwner != start.inputOwner);
    core.entities["Player"] = 7;
    core.entities["location"] = 10;
    assert(readGameplaySnapshot(true).inputOwner != start.inputOwner);
    core.entities["location"] = 8;
    character.SetAttribute("location", "other-city");
    assert(readGameplaySnapshot(true).inputOwner != start.inputOwner);
    character.SetAttribute("location", "city");
    assert(readGameplaySnapshot(true).inputOwner == start.inputOwner);

    // Real removal of a mapped control remains authoritative during the gap.
    auto *primary = groups->GetAttributeClass("PrimaryLand");
    primary->DeleteAttributeClassX(primary->GetAttributeClass("ChrForward"));
    const auto restricted = readGameplaySnapshot(true);
    assert(std::find(restricted.capabilities.begin(), restricted.capabilities.end(), "ChrForward") == restricted.capabilities.end());
    std::cout << "PASS actual gameplay snapshot stance gaps, movement mappings, hard guards and stable scene/player ownership\n";
}
