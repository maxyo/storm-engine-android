#include "aviplayer.h"

#include "app_events.hpp"
#include "core.h"
#include "entity.h"
#include "shared/interface/messages.h"
#include "storm/video/player.hpp"
#include "v_file_service.h"
#include "v_sound_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

#define VIDEO_DIRECTORY "resource\\videos"

namespace
{
constexpr auto XI_AVIVIDEO_FVF = D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXTUREFORMAT2;
struct XI_AVIVIDEO_VERTEX
{
    float x, y, z, rhw;
    float tu, tv;
};

void videoLog(storm::video::LogLevel level, const std::string &message)
{
    switch (level)
    {
    case storm::video::LogLevel::Info:
        spdlog::info("Video: {}", message);
        break;
    case storm::video::LogLevel::Warning:
        spdlog::warn("Video: {}", message);
        break;
    case storm::video::LogLevel::Error:
        spdlog::error("Video: {}", message);
        break;
    }
}

// The soundtrack goes through the engine's sound service (miniaudio) at the music volume.
class SoundStreamSink final : public storm::video::AudioSink
{
  public:
    explicit SoundStreamSink(std::unique_ptr<VSoundStream> stream) : stream_(std::move(stream))
    {
    }
    uint32_t write(const float *pcm, uint32_t frames) override
    {
        return stream_->Write(pcm, frames);
    }
    uint64_t playedFrames() override
    {
        return stream_->PlayedFrames();
    }
    void setPaused(bool paused) override
    {
        stream_->SetPaused(paused);
    }

  private:
    std::unique_ptr<VSoundStream> stream_;
};
static_assert(VSoundStream::kRate == storm::video::kAudioRate);
} // namespace

CAviPlayer::CAviPlayer() = default;

CAviPlayer::~CAviPlayer()
{
    if (appEvents_ >= 0)
        storm::AppEvents::instance().unsubscribe(appEvents_);
    player_.reset();
    ReleaseTexture();
}

bool CAviPlayer::Init()
{
    if ((rs = static_cast<VDX9RENDER *>(core.GetService("dx9render"))) == nullptr)
    {
        throw std::runtime_error("Can`t create render service");
    }
    storm::video::setLogSink(&videoLog);

    core.SetLayerType(VIDEO_REALIZE, layer_type_t::realize);
    core.AddToLayer(VIDEO_REALIZE, GetId(), -1);
    core.SetLayerType(VIDEO_EXECUTE, layer_type_t::execute);
    core.AddToLayer(VIDEO_EXECUTE, GetId(), 1);

    // Background (also the app's modal pause): the clock and the soundtrack stop until the
    // activity returns; the shown frame is uploaded again in case the surface was recreated.
    appEvents_ = storm::AppEvents::instance().subscribe([this](const storm::AppEvent &event) {
        if (event.type == storm::AppEventType::WillEnterBackground)
            background_ = true;
        else if (event.type == storm::AppEventType::DidEnterForeground)
            background_ = false;
        else
            return;
        if (player_)
            player_->setPaused(background_);
        reupload_ = true;
    });
    return true;
}

bool CAviPlayer::Start()
{
    const std::string path = fio->ConvertPathResource(filename.c_str());
    std::unique_ptr<storm::video::AudioSink> audio;
    if (auto *sound = static_cast<VSoundService *>(core.GetService("SoundService")))
    {
        if (auto stream = sound->CreatePcmStream())
        {
            float fx, music, speech;
            sound->GetMasterVolume(&fx, &music, &speech);
            stream->SetVolume(music);
            audio = std::make_unique<SoundStreamSink>(std::move(stream));
        }
    }
    const POINT screen = rs->GetScreenSize();
    storm::video::Options options;
    options.maxWidth = std::max<int>(screen.x, 2);
    options.maxHeight = std::max<int>(screen.y, 2);
    player_ = storm::video::Player::open(storm::video::openFile(path), filename, std::move(audio), options);
    if (player_ && background_)
        player_->setPaused(true);
    return player_ != nullptr;
}

uint64_t CAviPlayer::ProcessMessage(MESSAGE &message)
{
    switch (message.Long())
    {
    case MSG_SET_VIDEO_PLAY: {
        const std::string &param = message.String();
        filename = fmt::format("{}\\{}", VIDEO_DIRECTORY, param);
        player_.reset();
        ended_ = false;
        tapState_ = 0;
        if (!Start())
        {
            ended_ = true;
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

void CAviPlayer::Execute()
{
    if (!player_ || ended_)
        return;
    if (player_->finished())
    {
        Finish();
        return;
    }
    if (m_bShowVideo)
        DetectTapToSkip();
}

void CAviPlayer::Finish()
{
    const bool failed = player_->failed();
    player_.reset();
    if (bLoop && !failed && Start())
        return;
    ended_ = true;
    core.Event("ievntEndVideo");
}

// A tap on the video is the touch equivalent of Esc: the scripts' own video-break handlers
// ("Control Activation"/"Control Deactivation" of ICancel) decide whether this video may be
// skipped. A touch already down when the video starts does not count.
void CAviPlayer::DetectTapToSkip()
{
    if (!core.Controls)
        return;
    const bool down = core.Controls->IsKeyPressed(VK_LBUTTON);
    if (tapState_ == 0 || tapState_ == 1)
    {
        tapState_ = down ? (tapState_ == 1 ? 2 : 0) : 1;
        return;
    }
    if (down)
        return;
    tapState_ = 1;
    spdlog::info("Video: tap on {}, sending ICancel to the video-break handlers", filename);
    core.Event("Control Activation", "s", "ICancel");
    core.Event("Control Deactivation", "s", "ICancel");
}

void CAviPlayer::Upload(const storm::video::Frame &frame)
{
    if (!texture_ || textureWidth_ != frame.width || textureHeight_ != frame.height)
    {
        ReleaseTexture();
        // A8B8G8R8 is R,G,B,A in memory, the decoder's output order.
        if (FAILED(rs->CreateTexture(frame.width, frame.height, 1, 0, D3DFMT_A8B8G8R8, D3DPOOL_MANAGED, &texture_)))
        {
            spdlog::error("Video: cannot create a {}x{} texture for {}", frame.width, frame.height, filename);
            texture_ = nullptr;
            return;
        }
        textureWidth_ = frame.width;
        textureHeight_ = frame.height;
    }
    D3DLOCKED_RECT locked;
    if (FAILED(texture_->LockRect(0, &locked, nullptr, 0)))
    {
        spdlog::error("Video: cannot lock the texture of {}", filename);
        return;
    }
    const size_t row = static_cast<size_t>(frame.width) * 4;
    auto *target = static_cast<uint8_t *>(locked.pBits);
    for (int y = 0; y < frame.height; ++y)
        std::memcpy(target + static_cast<size_t>(y) * locked.Pitch, frame.rgba.data() + y * row, row);
    texture_->UnlockRect(0);
}

void CAviPlayer::Realize()
{
    if (!player_)
        return;
    bool changed = false;
    const storm::video::Frame *frame = player_->update(&changed);
    if (frame && (changed || reupload_))
    {
        Upload(*frame);
        reupload_ = false;
    }
    if (!m_bShowVideo)
        return;
    rs->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 0.f, 0);
    if (!texture_)
        return;
    // Letterboxed to the screen, keeping the display aspect ratio.
    const POINT screen = rs->GetScreenSize();
    const float aspect = static_cast<float>(player_->info().aspect);
    float width = static_cast<float>(screen.x), height = width / aspect;
    if (height > screen.y)
    {
        height = static_cast<float>(screen.y);
        width = height * aspect;
    }
    const float left = (screen.x - width) * .5f, top = (screen.y - height) * .5f;
    XI_AVIVIDEO_VERTEX v[4] = {
        {left, top, 1.f, .5f, 0.f, 0.f},
        {left, top + height, 1.f, .5f, 0.f, 1.f},
        {left + width, top, 1.f, .5f, 1.f, 0.f},
        {left + width, top + height, 1.f, .5f, 1.f, 1.f},
    };
    rs->SetTexture(0, texture_);
    rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, XI_AVIVIDEO_FVF, 2, v, sizeof(XI_AVIVIDEO_VERTEX), "battle_icons");
}

void CAviPlayer::ReleaseTexture()
{
    if (texture_ && rs)
        rs->Release(texture_);
    texture_ = nullptr;
    textureWidth_ = textureHeight_ = 0;
}
