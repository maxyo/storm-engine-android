// Title screen (interface "mainmenu", mockup V2-Menu-Title): the menu column over the
// game's animated menu scene. The engine publishes no widgets for this interface; it takes
// five commands instead (`main:New|Load|Options|Credits|Exit`, see EngineCommand), which
// the screen sends through command widgets of its own. «Продолжить» opens the saves and
// loads the newest record of any profile there (menu.h) and names it under its label (the
// engine's ANDROID_CONTINUE text «<hero> · <location>»; disabled without a record); «О программе» asks the platform host for
// its about and licences page. Fills the pack screen <s-title>.
#include "controls.h"
#include "menu.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"TITLE", "heading|text"}, // the game's own art names the game
    {"ANDROID_CONTINUE", "text"},
};

Widget Command(const char *name)
{
    Widget widget;
    widget.node = name;
    widget.type = "button";
    return widget;
}

class TitleScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-title";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        SetFromTitle(true);
        const auto button = [&bindings](const char *id, const char *label, const Widget &command, std::function<void()> before = {}) {
            bindings.On(id, "", [&bindings, &command, before](const Event &) {
                if (before)
                    before();
                bindings.Send(command, "press");
            });
            return Json{{"id", id}, {"label", bindings.Text(label)}, {"busy", bindings.Pending(id)}};
        };
        Json view = Json::object();
        view["primary"] = button("continue", "menu.continue", load_, [] { SetContinueRequested(true); });
        // Without a record there is nothing to continue: the button stays, disabled, and a tap
        // says why (the engine sends ANDROID_CONTINUE only when a record exists).
        const bool any = admitted.One("ANDROID_CONTINUE") != nullptr;
        view["primary"]["sub"] = admitted.Label("ANDROID_CONTINUE");
        view["primary"]["disabled"] = !any;
        view["primary"]["reason"] = any ? std::string() : bindings.Text("menu.no_saves");
        view["buttons"] = Json::array({button("new", "menu.new_game", new_), button("load", "menu.load", load_),
                                       button("options", "menu.settings", options_), button("credits", "menu.credits", credits_)});
        view["exit"] = button("exit", "menu.exit", exit_);
        view["about"] = Json{{"id", "about"}, {"label", bindings.Text("menu.about")}};
        bindings.On("about", "", [&bindings](const Event &) { bindings.SendHost("app-about", ""); });
        return view;
    }

  private:
    const Widget new_ = Command("New"), load_ = Command("Load"), options_ = Command("Options"), credits_ = Command("Credits"),
                 exit_ = Command("Exit");
};

} // namespace

std::unique_ptr<Screen> CreateTitleScreen()
{
    return std::make_unique<TitleScreen>();
}

} // namespace storm::ui::screens
