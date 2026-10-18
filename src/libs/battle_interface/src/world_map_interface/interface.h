#pragma once

#include "bi_defines.h"
#ifdef __ANDROID__
#include "platform/android_worldmap.hpp"
#include <chrono>
#endif

class WMShipIcon;
class WMShipCommandList;

class WM_INTERFACE : public Entity
{
    VDX9RENDER *rs;

  public:
    WM_INTERFACE();
    ~WM_INTERFACE() override;
    bool Init() override;
#ifdef __ANDROID__
    std::string androidCommandContext;
    std::chrono::steady_clock::time_point androidCommandPoll{};
    void ReadAndroidMapCommands(storm::android::WorldMapFrame &frame);
    bool ActivateAndroidMapCommand(const std::string &id);
#endif
    void Realize(uint32_t delta_time);
    uint64_t ProcessMessage(MESSAGE &message) override;
    uint32_t AttributeChanged(ATTRIBUTES *pAttr) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
            // case Stage::execute:
            //    Execute(delta); break;
        case Stage::realize:
            Realize(delta);
            break;
            /*case Stage::lost_render:
              LostRender(delta); break;
            case Stage::restore_render:
              RestoreRender(delta); break;*/
        }
    }

  protected:
    WMShipIcon *m_pShipIcon; // icons for commands and targeting in the combat menu
    WMShipCommandList *m_pCommandList;
    bool m_bVisible;
    int32_t m_nCommandListVerticalOffset;
    int32_t m_nMainCharIndex;
    int32_t m_nCommandMode;

  protected:
    void LoadIniFile();

    void MakeControl();
    void ExecuteCommand(int32_t command);
    void UpdateCommandList() const;
    int32_t GetCurrentCommandTopLine() const;
    int32_t GetCurrentCommandCharacterIndex() const;
    int32_t GetCurrentCommandMode() const;

    bool IsCommandMenuActive() const;
};
