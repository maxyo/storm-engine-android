#include "aviplayer.h"
#include <cstdio>

#include "core.h"

#include "entity.h"
#include "shared/interface/messages.h"

#include <spdlog/spdlog.h>
#include <string>
#ifdef __ANDROID__
#include <platform/android_gameplay.hpp>
#include <platform/android_ui.hpp>
#endif

#define VIDEO_DIRECTORY "resource\\videos"

CAviPlayer::CAviPlayer()
    : rs(nullptr)
{
    m_bContinue = true;
    m_bShowVideo = true;
}

CAviPlayer::~CAviPlayer()
{
    ReleaseAll();
}

bool CAviPlayer::Init()
{
    if ((rs = static_cast<VDX9RENDER *>(core.GetService("dx9render"))) == nullptr)
    {
        throw std::runtime_error("Can`t create render service");
    }

    // core.LayerCreate("vRealize",true,false);
    core.SetLayerType(VIDEO_REALIZE, layer_type_t::realize);
    core.AddToLayer(VIDEO_REALIZE, GetId(), -1);

    // core.LayerCreate("vExecute",true,false);
    core.SetLayerType(VIDEO_EXECUTE, layer_type_t::execute);
    core.AddToLayer(VIDEO_EXECUTE, GetId(), 1);

    if (!GetInterfaces())
    {
        m_bContinue = false;
        CleanupInterfaces();
    }

    return true;
}

void CAviPlayer::Execute(uint32_t delta_time)
{
    if (m_bContinue == false)
    {
        CleanupInterfaces();
        core.Event("ievntEndVideo");
    }
}

void CAviPlayer::Realize(uint32_t delta_time)
{
    //~!~
    // rs->BeginScene();
    m_bContinue = false;
}

uint64_t CAviPlayer::ProcessMessage(MESSAGE &message)
{
    switch (message.Long())
    {
    case MSG_SET_VIDEO_PLAY: {
        const std::string &param = message.String();
        const std::string vidName = fmt::format("{}\\{}", VIDEO_DIRECTORY, param);
        filename = vidName;
        if (!PlayMedia(vidName.c_str()))
        {
            CleanupInterfaces();
            core.PostEvent("ievntEndVideo", 1, nullptr);
        }
        break;
    }
    case MSG_SET_VIDEO_FLAGS: {
        constexpr auto loop_flag = 1 << 0;
        auto flags = message.Long();
        bLoop = flags & loop_flag;
        break;
    }
    }
    return 0;
}

bool CAviPlayer::PlayMedia(const char *fileName)
{
    // No video decoder is built yet (the game's videos are WMV3 + WMA2). The script then ends
    // the video at once (ievntEndVideo); the player is told, never left with a silent skip.
    const std::string name = std::string(fileName).substr(std::string(fileName).find_last_of("\\/") + 1);
    spdlog::error("Video {}: not played, no video decoder on this platform", fileName);
#ifdef __ANDROID__
    storm::android::publishUiState("engineNotice", "{\"kind\":\"video_unavailable\",\"subject\":" +
                                                       storm::android::gameplayJsonString(name) + "}");
#endif
    return false;
}

bool CAviPlayer::GetInterfaces()
{
    return false;
}

void CAviPlayer::CleanupInterfaces()
{
}

void CAviPlayer::ReleaseAll()
{
    CleanupInterfaces();
}
