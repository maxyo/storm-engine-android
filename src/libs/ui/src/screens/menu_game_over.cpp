// Game over (mockup V2-Menu-GameOver): the title of the defeat centred on a dark screen
// while the game shows its death picture and then returns to the title screen by itself.
// State channel "gameOver" (xinterface info_handler.cpp): {kind: sea|land|defeat, language}
// while the picture is up, null after. The consumer makes an interface model of its own
// ("game_over", one KIND text) that the screen turns into the pack screen <s-game-over>.
// Nothing to press: the game decides when it ends.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"KIND", "text", true},
};

class GameOverScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-game-over";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }
    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        static const char *const kTitles[][2] = {{"sea", StringKey("menu.game_over_sea")},
                                                 {"land", StringKey("menu.game_over_land")},
                                                 {"defeat", StringKey("menu.game_over_defeat")}};
        const std::string kind = admitted.Label("KIND");
        std::string title;
        for (const auto &[known, key] : kTitles)
            if (kind == known)
                title = bindings.Text(key);
        bindings.Ignore("header-back", ""); // the game leaves this screen by itself
        return Json{{"title", title}};
    }
};

class GameOverConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "gameOver";
    }
    void OnState(const Json &state, Router &router) override
    {
        const std::string kind = state.is_object() && state.contains("kind") && state["kind"].is_string() ? state["kind"].get<std::string>() : "";
        if (state.is_null() || (kind != "sea" && kind != "land" && kind != "defeat"))
        {
            if (!state.is_null())
                router.ReportError("state channel gameOver: expected {kind: sea|land|defeat, language} or null, got " + state.dump());
            if (router.Using() == "game_over")
                router.CloseInterface();
            return;
        }
        InterfaceModel model;
        model.screen = "game_over";
        model.language = state.contains("language") && state["language"].is_string() ? state["language"].get<std::string>() : std::string();
        model.opening = ++opening_;
        model.revision = opening_;
        Widget text;
        text.node = "KIND";
        text.type = "text";
        text.label = kind;
        model.widgets.push_back(text);
        router.SetInterface(model);
    }

  private:
    uint64_t opening_ = 0;
};

} // namespace

std::unique_ptr<Screen> CreateGameOverScreen()
{
    return std::make_unique<GameOverScreen>();
}

std::unique_ptr<StateConsumer> CreateGameOverConsumer()
{
    return std::make_unique<GameOverConsumer>();
}

} // namespace storm::ui::screens
