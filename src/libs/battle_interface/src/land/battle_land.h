#pragma once

#include "bi_utils.h"
#include "bi_defines.h"
#ifdef __ANDROID__
#include <platform/android_land_commands.hpp>
#endif

class BIManSign;

class BATTLE_LAND_INTERFACE : public Entity
{
    // metods
  public:
    BATTLE_LAND_INTERFACE();
    ~BATTLE_LAND_INTERFACE() override;
    bool Init() override;
    void Execute(uint32_t delta_time) const;
    void Realize(uint32_t delta_time);
    uint64_t ProcessMessage(MESSAGE &message) override;
#ifdef __ANDROID__
    storm::android::LandCommandSource ReadAndroidLand(storm::android::LandCommandsFrame &frame);
    bool ExecuteAndroidLand(const storm::android::LandCommandsRequest &request);
    void CloseAndroidLand();
#endif

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::execute:
            Execute(delta);
            break;
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
    void SetShowParameters();
    void SetParameters();
    void UpdateCommandos() const;
    void UpdateAlarm();
    void EndShow();
    void Release();
    void EnableMessageIcons(VDATA *pvdat);
    void DisableMessageIcons();
    void SetTextData();

    // data
  protected:
    VDX9RENDER *m_pRS;
    bool m_bShowCommandos;

    BIManSign *m_pManSign;

    std::vector<BITextInfo> m_TextInfo;

    BIImagesInfo m_Images;
};
