#pragma once

#include "../base_video.h"

#include <memory>
#include <string>

namespace storm::video
{
class Player;
struct Frame;
}

// Script-facing video player entity ("CAviPlayer"). Scripts send MSG_SET_VIDEO_PLAY with a file
// name under resource\videos and receive "ievntEndVideo" when it has played out (or failed);
// MSG_SET_VIDEO_FLAGS bit 0 loops. xi_video_rect embeds one with SetShowVideo(false) and draws
// GetCurrentVideoTexture() itself. Decoding: src/libs/video.
class CAviPlayer : public xiBaseVideo
{
  public:
    CAviPlayer();
    ~CAviPlayer() override;
    bool Init() override;
    uint64_t ProcessMessage(MESSAGE &message) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::execute:
            Execute();
            break;
        case Stage::realize:
            Realize();
            break;
        default:
            break;
        }
    }

    void SetShowVideo(bool bShowVideo) override
    {
        m_bShowVideo = bShowVideo;
    }

    IDirect3DTexture9 *GetCurrentVideoTexture() override
    {
        return texture_;
    }

  private:
    void Execute();
    void Realize();
    bool Start();
    void Finish();
    void Upload(const storm::video::Frame &frame);
    void ReleaseTexture();
    void DetectTapToSkip();

    VDX9RENDER *rs = nullptr;
    bool m_bShowVideo = true;
    bool bLoop = false;
    std::string filename;
    std::unique_ptr<storm::video::Player> player_;
    IDirect3DTexture9 *texture_ = nullptr;
    int textureWidth_ = 0, textureHeight_ = 0;
    bool ended_ = false;        // ievntEndVideo sent
    bool background_ = false;   // application in background (AppEvents)
    bool reupload_ = false;     // upload the shown frame again (surface may have been recreated)
    int appEvents_ = -1;
    int tapState_ = 0;          // 0: wait for release, 1: wait for press, 2: pressed
};
