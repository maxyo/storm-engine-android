#pragma once

#include "../battle_command.h"
#ifdef __ANDROID__
#include <platform/android_land_commands.hpp>
#endif

class BIManCommandList : public BICommandList
{
  public:
    BIManCommandList(entid_t eid, ATTRIBUTES *pA, VDX9RENDER *rs);
    ~BIManCommandList() override;

    void FillIcons() override;
    void Init() override;
#ifdef __ANDROID__
    storm::android::LandCommandSource RefreshAndroidSource();
    bool SelectAndroidLand(const std::string &id);
    void ReadAndroidLand(storm::android::LandCommandsFrame &frame) const;
#endif

  protected:
    void Release();

    int32_t CommandAdding();
    int32_t UserIconsAdding();
    int32_t AbilityAdding();
    int32_t AddCancelIcon();
};
