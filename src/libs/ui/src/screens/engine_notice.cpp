// State channel `engineNotice`: something the engine could not do that the player must hear
// about instead of a silent skip, {kind, subject}. Kinds:
//   video_unavailable  a game video (subject: its file) was not played: no video decoder is
//                      built for this platform yet (xinterface aviplayer.cpp). The documented,
//                      logged exception until the player exists.
// Each notice is a toast; an unknown kind is an error line.
#include "screen.h"

namespace storm::ui::screens
{

namespace
{

class EngineNoticeConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "engineNotice";
    }
    void OnState(const Json &state, Router &router) override
    {
        const auto text = [&state](const char *key) {
            return state.is_object() && state.contains(key) && state[key].is_string() ? state[key].get<std::string>() : std::string();
        };
        if (text("kind") == "video_unavailable")
            router.Notify(StringKey("notice.video_unavailable"), text("subject"));
        else
            router.ReportError("state channel engineNotice: expected {kind: video_unavailable, subject}, got " + state.dump());
    }
};

} // namespace

std::unique_ptr<StateConsumer> CreateEngineNoticeConsumer()
{
    return std::make_unique<EngineNoticeConsumer>();
}

} // namespace storm::ui::screens
