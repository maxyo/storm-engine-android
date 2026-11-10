// "DynamicInterfaceService": the registry of Caribbean Legend's custom dynamic HUD groups.
//
// Script contract (Program/battle_interface/DynamicInterface.c of the game, message ids in
// shared/battle_interface/msg_control.h):
//   UI_GROUP_REGISTER "lsall" name, root aref, host mask (UI_GROUP_LAND/SEA/WM/LOG sum),
//                     refresh mode (UI_GROUP_REQUEST/FRAME)  -> 1 registered, 0 rejected
//   UI_GROUP_EXISTS   "ls"    name -> 1 / 0
//   UI_GROUP_REFRESH  "ls"    name: re-read a REQUEST group from its root
//   UI_GROUP_REMOVE   "ls"    name: forget it (removing an unknown name is allowed: the scripts
//                             clear groups "just in case")
// The scripts create the entity without layers and it needs none: it only keeps the model.
//
// Drawing: none here. The groups (and the battle interfaces' own "dynamic" subtrees) are exposed
// as a storm::dynamic_hud::Snapshot for the portable UI, which replaces the legacy battle
// interface; until the UI stream draws them every group is logged once as not drawn.
#pragma once

#include "dynamic_hud_model.hpp"

#include "entity.h"

#include <map>
#include <set>
#include <string>

class DynamicInterfaceService final : public Entity
{
  public:
    bool Init() override;
    void ProcessStage(Stage, uint32_t) override
    {
    }
    uint64_t ProcessMessage(MESSAGE &message) override;

    // Registered groups (FRAME groups re-read now, REQUEST groups as of their last refresh) and
    // the "dynamic" subtrees of the battle interfaces that exist. Roots are read here, so the
    // scripts' rule applies: a group is unregistered before its attributes are deleted.
    [[nodiscard]] storm::dynamic_hud::Snapshot Snapshot();

  private:
    struct Registration
    {
        ATTRIBUTES *root = nullptr;
        storm::dynamic_hud::Group group;
    };

    bool Register(const std::string &name, ATTRIBUTES *root, int32_t hosts, int32_t mode);
    storm::dynamic_hud::Group Read(const ATTRIBUTES &root, const std::string &name, uint32_t hosts,
                                   storm::dynamic_hud::Refresh refresh);

    std::map<std::string, Registration> groups_;
    std::set<std::string> reported_; // not-drawn notices and read errors, each logged once
    uint64_t revision_ = 0;
};
