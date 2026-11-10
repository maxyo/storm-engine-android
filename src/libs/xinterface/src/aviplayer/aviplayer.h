#pragma once

#include "../base_video.h"

class CAviPlayer : public xiBaseVideo
{
    VDX9RENDER *rs;
    bool m_bShowVideo;

  public:
    CAviPlayer();
    ~CAviPlayer() override;
    bool Init() override;
    void Execute(uint32_t delta_time);
    void Realize(uint32_t delta_time);
    uint64_t ProcessMessage(MESSAGE &message) override;

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

    void SetShowVideo(bool bShowVideo) override
    {
        m_bShowVideo = bShowVideo;
    }

    IDirect3DTexture9 *GetCurrentVideoTexture() override
    {
        return nullptr;
    }

  protected:
    bool m_bContinue;

    void ReleaseAll();
    bool PlayMedia(const char *fileName);
    bool GetInterfaces();
    void CleanupInterfaces();

    std::string filename;
    bool bLoop{false};
};
