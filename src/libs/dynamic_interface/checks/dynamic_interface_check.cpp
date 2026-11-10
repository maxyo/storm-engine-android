// Host check of DynamicInterfaceService through the real core: the UI_GROUP_* contract, the
// structured model of registered groups and of a battle interface's "dynamic" subtree, and the
// rejection of bad registrations.
#include "dynamic_interface_service.hpp"

#include "attributes.h"
#include "core_private.h"
#include "message.h"
#include "shared/battle_interface/msg_control.h"
#include "vma.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace storm::dynamic_hud;

namespace
{
int failures = 0;

void Check(bool condition, const char *what)
{
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    failures += !condition;
}

class Codec final : public VSTRING_CODEC
{
  public:
    uint32_t GetNum() override
    {
        return static_cast<uint32_t>(names_.size());
    }
    uint32_t Convert(const char *name) override
    {
        const auto found = std::find(names_.begin(), names_.end(), name);
        if (found != names_.end())
            return static_cast<uint32_t>(found - names_.begin());
        names_.emplace_back(name);
        return static_cast<uint32_t>(names_.size() - 1);
    }
    uint32_t Convert(const char *name, int32_t length) override
    {
        return Convert(std::string(name, length).c_str());
    }
    const char *Convert(uint32_t code) override
    {
        return names_.at(code).c_str();
    }
    void VariableChanged() override
    {
    }

  private:
    std::vector<std::string> names_;
};
Codec codec;

const Group *Find(const Snapshot &snapshot, const std::string &name)
{
    for (const auto &group : snapshot.groups)
        if (group.name == name)
            return &group;
    return nullptr;
}
} // namespace

// Stands in for the land interface host: only its attributes matter here.
class battle_land_interface final : public Entity
{
  public:
    bool Init() override
    {
        return true;
    }
    void ProcessStage(Stage, uint32_t) override
    {
    }
};
CREATE_CLASS(battle_land_interface)

int main()
{
    const auto started = std::chrono::steady_clock::now();
    auto *core_private = static_cast<CorePrivate *>(&core);
    core_private->Init();
    core_private->InitBase();

    const auto service = core.CreateEntity("DynamicInterfaceService");
    Check(service != invalid_entity, "core creates 'DynamicInterfaceService'");
    auto *entity = static_cast<DynamicInterfaceService *>(core.GetEntityPointer(service));

    // A notification-like group: back image from a picture group, a right-aligned text, script data.
    ATTRIBUTES root(codec);
    root.SetAttribute("visible", "1");
    root.SetAttribute("layer", "100");
    root.CreateSubAClass(&root, "images.back.group")->SetValue("NOTES");
    root.CreateSubAClass(&root, "images.back.picture")->SetValue("back");
    root.CreateSubAClass(&root, "images.back.pos")->SetValue("10,20,110,60");
    root.CreateSubAClass(&root, "images.tex.texture")->SetValue("interfaces\\x.tga");
    root.CreateSubAClass(&root, "images.tex.uv")->SetValue("0.0,0.5,1.0,1.0");
    root.CreateSubAClass(&root, "texts.note.text")->SetValue("Hello");
    root.CreateSubAClass(&root, "texts.note.font")->SetValue("interface_normal");
    root.CreateSubAClass(&root, "texts.note.scale")->SetValue("0.750000");
    root.CreateSubAClass(&root, "texts.note.color")->SetValue("-1");
    root.CreateSubAClass(&root, "texts.note.pos")->SetValue("1820,880");
    root.CreateSubAClass(&root, "texts.note.align")->SetValue("right");
    root.CreateSubAClass(&root, "texts.note.base.pos.y")->SetValue("42");
    root.CreateSubAClass(&root, "fade.elapsed")->SetValue("0.0");

    Check(core.Send_Message(service, "ls", UI_GROUP_EXISTS, "note_0") == 0, "EXISTS before REGISTER: 0");
    Check(core.Send_Message(service, "lsall", UI_GROUP_REGISTER, "note_0", &root, 8, 1) == 1,
          "REGISTER (LOG, FRAME): 1");
    Check(core.Send_Message(service, "ls", UI_GROUP_EXISTS, "note_0") == 1, "EXISTS after REGISTER: 1");

    auto snapshot = entity->Snapshot();
    const Group *group = Find(snapshot, "note_0");
    Check(group && group->hosts == kLog && group->refresh == Refresh::frame && group->visible && group->layer == 100,
          "group header: hosts, refresh, visible, layer");
    Check(group && group->images.size() == 2 && group->images[0].group == "NOTES" && group->images[0].pos &&
              group->images[0].pos->right == 110 && group->images[1].uv && (*group->images[1].uv)[1] == 0.5f,
          "images: picture group + rect, texture + uv");
    Check(group && group->texts.size() == 1 && group->texts[0].text == "Hello" && group->texts[0].color == 0xffffffff &&
              group->texts[0].pos && group->texts[0].pos->x == 1820 && group->texts[0].align == "right" &&
              group->texts[0].scale == 0.75f && group->texts[0].extra.at("base.pos.y") == "42",
          "texts: text, font, scale, ARGB color, pos, align, extra layout fields");
    Check(group && group->script_data == std::vector<std::string>{"fade"}, "non-drawable children are script data");

    // FRAME groups are re-read for every snapshot; REQUEST groups only on UI_GROUP_REFRESH.
    root.CreateSubAClass(&root, "texts.note.text")->SetValue("Changed");
    snapshot = entity->Snapshot();
    Check(Find(snapshot, "note_0")->texts[0].text == "Changed", "FRAME group follows its attributes");
    ATTRIBUTES tutorial(codec);
    tutorial.CreateSubAClass(&tutorial, "texts.t1.text")->SetValue("one");
    Check(core.Send_Message(service, "lsall", UI_GROUP_REGISTER, "tutorial", &tutorial, 1 + 2 + 4, 0) == 1,
          "REGISTER (LAND+SEA+WM, REQUEST): 1");
    tutorial.CreateSubAClass(&tutorial, "texts.t1.text")->SetValue("two");
    Check(Find(entity->Snapshot(), "tutorial")->texts[0].text == "one", "REQUEST group keeps its last read");
    core.Send_Message(service, "ls", UI_GROUP_REFRESH, "tutorial");
    Check(Find(entity->Snapshot(), "tutorial")->texts[0].text == "two", "REFRESH re-reads a REQUEST group");

    // Rejected registrations.
    Check(core.Send_Message(service, "lsall", UI_GROUP_REGISTER, "bad_hosts", &root, 0, 0) == 0, "host mask 0: 0");
    Check(core.Send_Message(service, "lsall", UI_GROUP_REGISTER, "bad_hosts", &root, 16, 0) == 0,
          "host mask outside LAND|SEA|WM|LOG: 0");
    Check(core.Send_Message(service, "lsall", UI_GROUP_REGISTER, "bad_mode", &root, 8, 2) == 0, "refresh mode 2: 0");
    Check(core.Send_Message(service, "ls", UI_GROUP_EXISTS, "bad_mode") == 0, "rejected group is not registered");

    // Malformed values are reported, the rest of the group stays usable.
    ATTRIBUTES broken(codec);
    broken.CreateSubAClass(&broken, "texts.a.pos")->SetValue("12");
    broken.CreateSubAClass(&broken, "texts.a.align")->SetValue("middle");
    broken.CreateSubAClass(&broken, "images.b.color")->SetValue("red");
    Report report;
    const auto parsed = ParseGroup(broken, "broken", kLog, Refresh::request, report);
    Check(report.errors.size() == 4 && parsed.texts.size() == 1 && parsed.images.size() == 1,
          "bad pos, align, color and an image without texture/group are 4 reported errors");

    // A host's own "dynamic" subtree is part of the model.
    const auto land = core.CreateEntity("battle_land_interface");
    ATTRIBUTES land_attributes(codec);
    core.GetEntityPointer(land)->AttributesPointer = &land_attributes;
    land_attributes.CreateSubAClass(&land_attributes, "dynamic.officer_orders.visible")->SetValue("0");
    land_attributes.CreateSubAClass(&land_attributes, "dynamic.officer_orders.images.slot1.group")->SetValue("ICONS");
    snapshot = entity->Snapshot();
    group = Find(snapshot, "officer_orders");
    Check(group && group->hosts == kLand && !group->visible && group->images.size() == 1,
          "battle_land_interface.dynamic groups are modelled with the LAND host");

    const auto json = nlohmann::json::parse(snapshot.Json());
    Check(json["schemaVersion"] == 1 && json["groups"].size() == snapshot.groups.size() &&
              json["groups"][0]["hosts"].is_array(),
          "snapshot serialises as the dynamicHud channel JSON");

    core.Send_Message(service, "ls", UI_GROUP_REMOVE, "note_0");
    core.Send_Message(service, "ls", UI_GROUP_REMOVE, "never_registered");
    Check(core.Send_Message(service, "ls", UI_GROUP_EXISTS, "note_0") == 0, "REMOVE forgets the group");
    core.EraseEntity(land);
    core.EraseEntity(service);

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s: %d failure(s), %.2f s\n", failures ? "FAIL" : "PASS", failures, seconds);
    return failures ? 1 : 0;
}
