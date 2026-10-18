#pragma once

#include "../battle_command.h"
#ifdef __ANDROID__
#include "platform/android_worldmap.hpp"
#endif

class WMShipCommandList : public BICommandList
{
  public:
    WMShipCommandList(entid_t eid, ATTRIBUTES *pA, VDX9RENDER *rs);
    ~WMShipCommandList() override;

    void FillIcons() override;
    void Init() override;
#ifdef __ANDROID__
    std::vector<storm::android::MapCommand> AndroidMapCommands() const;
    bool SelectAndroidMapCommand(const std::string &id);
#endif

  protected:
    void Release();

    int32_t CommandAdding();
};
