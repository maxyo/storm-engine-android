#include "dynamic_interface_service.hpp"

#include "core.h"
#include "message.h"
#include "shared/battle_interface/msg_control.h"

#include <spdlog/spdlog.h>

using namespace storm::dynamic_hud;

namespace
{
// The battle interfaces that keep a "dynamic" subtree of groups, and where each is shown.
struct HostEntity
{
    const char *entity;
    uint32_t hosts;
};
constexpr HostEntity kHostEntities[] = {
    {"battle_land_interface", kLand}, // objLandInterface.dynamic
    {"battle_interface", kSea},       // BattleInterface.dynamic at sea
    {"WM_INTERFACE", kWorldMap},      // BattleInterface.dynamic on the world map
    {"ILogAndActions", kLog},         // ILogAndActions.dynamic
};
} // namespace

bool DynamicInterfaceService::Init()
{
    spdlog::warn("DynamicInterfaceService: dynamic HUD groups are modelled but not drawn (dynamic HUD renderer "
                 "pending in the UI stream)");
    return true;
}

Group DynamicInterfaceService::Read(const ATTRIBUTES &root, const std::string &name, uint32_t hosts, Refresh refresh)
{
    Report report;
    auto group = ParseGroup(root, name, hosts, refresh, report);
    for (const auto &error : report.errors)
        if (reported_.insert(error).second)
            spdlog::error("DynamicInterfaceService: {}", error);
    return group;
}

bool DynamicInterfaceService::Register(const std::string &name, ATTRIBUTES *root, int32_t hosts, int32_t mode)
{
    if (name.empty() || !root)
    {
        spdlog::error("DynamicInterfaceService: UI_GROUP_REGISTER '{}' without {}", name, root ? "a name" : "a root");
        return false;
    }
    if (hosts <= 0 || (static_cast<uint32_t>(hosts) & ~kAllHosts) != 0)
    {
        spdlog::error("DynamicInterfaceService: UI_GROUP_REGISTER '{}': host mask {} is not a sum of "
                      "UI_GROUP_LAND/SEA/WM/LOG", name, hosts);
        return false;
    }
    if (mode != static_cast<int32_t>(Refresh::request) && mode != static_cast<int32_t>(Refresh::frame))
    {
        spdlog::error("DynamicInterfaceService: UI_GROUP_REGISTER '{}': refresh mode {} is not UI_GROUP_REQUEST "
                      "or UI_GROUP_FRAME", name, mode);
        return false;
    }
    if (groups_.contains(name))
        spdlog::info("DynamicInterfaceService: group '{}' registered again, the new root replaces the old", name);
    auto &registration = groups_[name];
    registration.root = root;
    registration.group = Read(*root, name, static_cast<uint32_t>(hosts), static_cast<Refresh>(mode));
    ++revision_;
    if (reported_.insert("not drawn: " + name).second)
        spdlog::warn("DynamicInterfaceService: group '{}' registered but not drawn (dynamic HUD renderer pending in "
                     "the UI stream)", name);
    return true;
}

uint64_t DynamicInterfaceService::ProcessMessage(MESSAGE &message)
{
    const auto code = message.Long();
    switch (code)
    {
    case UI_GROUP_REGISTER: {
        const std::string name = message.String();
        auto *root = message.AttributePointer();
        const auto hosts = message.Long();
        const auto mode = message.Long();
        return Register(name, root, hosts, mode) ? 1 : 0;
    }
    case UI_GROUP_EXISTS:
        return groups_.contains(message.String()) ? 1 : 0;
    case UI_GROUP_REFRESH: {
        const std::string name = message.String();
        const auto found = groups_.find(name);
        if (found == groups_.end())
        {
            spdlog::error("DynamicInterfaceService: UI_GROUP_REFRESH of unregistered group '{}'", name);
            return 0;
        }
        auto &[root, group] = found->second;
        group = Read(*root, name, group.hosts, group.refresh);
        ++revision_;
        return 0;
    }
    case UI_GROUP_REMOVE:
        revision_ += groups_.erase(message.String());
        return 0;
    default:
        spdlog::error("DynamicInterfaceService: message {} is not handled", code);
        return 0;
    }
}

Snapshot DynamicInterfaceService::Snapshot()
{
    storm::dynamic_hud::Snapshot snapshot;
    for (auto &[name, registration] : groups_)
    {
        if (registration.group.refresh == Refresh::frame)
            registration.group = Read(*registration.root, name, registration.group.hosts, Refresh::frame);
        snapshot.groups.push_back(registration.group);
    }
    for (const auto &host : kHostEntities)
    {
        const auto id = core.GetEntityId(host.entity);
        auto *entity = id ? core.GetEntityPointerSafe(id) : nullptr;
        if (!entity || !entity->AttributesPointer)
            continue;
        const auto *dynamic = entity->AttributesPointer->GetAttributeClass("dynamic");
        if (!dynamic)
            continue;
        for (size_t i = 0; i < dynamic->GetAttributesNum(); ++i)
        {
            const auto &root = *dynamic->GetAttributeClass(static_cast<uint32_t>(i));
            snapshot.groups.push_back(Read(root, root.GetThisName(), host.hosts, Refresh::frame));
        }
    }
    snapshot.revision = ++revision_;
    return snapshot;
}
