#pragma once

#include "../battle_command.h"
#ifdef __ANDROID__
#include <platform/android_land_commands.hpp>
#endif

class BIManCommandList : public BICommandList
{
  public:
    BIManCommandList(entid_t eid, ATTRIBUTES *pA, VRenderer *rs);
    ~BIManCommandList() override;

    void FillIcons() override;
    void Init() override;
#ifdef __ANDROID__
    storm::android::LandCommandSource RefreshAndroidSource();
    bool SelectAndroidLand(const std::string &id);
    void ReadAndroidLand(storm::android::LandCommandsFrame &frame) const;
    // The events of the root commands the scripts enable now (their Commands attributes).
    void ReadAndroidOffered(std::vector<std::string> &offered) const;
    // The open root stage's item of that command event ("i<n>"), "" = not in the list.
    std::string AndroidItemOf(const std::string &event) const;
#endif

  protected:
#ifdef __ANDROID__
    std::string AndroidPicture(const UsedCommand &command) const;
#endif
    void Release();

    int32_t CommandAdding();
    int32_t UserIconsAdding();
    int32_t AbilityAdding();
    int32_t AddCancelIcon();
};
