// Pause menu (interface "game_menu", mockups V2-Menu-Game and V2-Menu-ExitConfirm): the
// primary «Продолжить» over a 2 × 2 grid of save, load, settings and exit, centred over
// the paused scene. The game's exit question (CONFIRM_EXIT_*, or CONFIRM_WINDOW_* in some
// versions) becomes the danger C-Dialog. Fills the pack screen <s-game-menu>.
#include "controls.h"
#include "menu.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"MB_RESUME", "button", true},
    {"MB_SAVE", "button"},
    {"MB_LOAD", "button"},
    {"MB_OPTIONS", "button"},
    {"MB_EXITGAME", "button"},
    {"MB_QUIT", "button"},
    {"MB_UPDATES", "button"},
    {"CONFIRM_EXIT_TEXT", "text"},
    {"CONFIRM_EXIT_YES", "button"},
    {"CONFIRM_EXIT_NO", "button"},
    {"CONFIRM_WINDOW_STRINGS", "text"},
    {"CONFIRM_WINDOW_MB_YES", "button"},
    {"CONFIRM_WINDOW_MB_NO", "button"},
    {"CONFIRM_*", "image|text"},
    // The game's title picture and its version lines belong to its own art, not to the menu.
    {"LOGO", "image"},
    {"VERSION_TEXT", "text"},
    {"DLC_TEXT", "text"},
    {"COPYRIGHTS_TEXT", "text"},
};

std::string Joined(const Admission &admitted, const char *node)
{
    std::string text;
    for (const Widget *line : admitted.All(node))
        if (!line->label.empty())
            text += (text.empty() ? "" : "\n") + line->label;
    return text;
}

class GameMenuScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-game-menu";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        SetFromTitle(false);
        Json view = Json::object();
        const Widget *resume = admitted.One("MB_RESUME");
        view["resume"] = ButtonView(bindings, *resume, "resume", "primary");

        Json buttons = Json::array();
        const Widget *exit = admitted.One("MB_EXITGAME") ? admitted.One("MB_EXITGAME") : admitted.One("MB_QUIT");
        const std::pair<const char *, const Widget *> grid[] = {
            {"save", admitted.One("MB_SAVE")}, {"load", admitted.One("MB_LOAD")}, {"options", admitted.One("MB_OPTIONS")}, {"exit", exit}};
        for (const auto &[id, button] : grid)
            if (button)
                buttons.push_back(ButtonView(bindings, *button, id, "secondary"));
        // Optional in game variants; shown only while the game offers it.
        if (const Widget *updates = admitted.One("MB_UPDATES"); updates && updates->Offers("press"))
            buttons.push_back(ButtonView(bindings, *updates, "updates", "secondary"));
        view["buttons"] = buttons;

        const bool window = admitted.One("CONFIRM_WINDOW_MB_YES") != nullptr;
        const Widget *yes = window ? admitted.One("CONFIRM_WINDOW_MB_YES") : admitted.One("CONFIRM_EXIT_YES");
        const Widget *no = window ? admitted.One("CONFIRM_WINDOW_MB_NO") : admitted.One("CONFIRM_EXIT_NO");
        if (yes)
        {
            const std::string question = Joined(admitted, window ? "CONFIRM_WINDOW_STRINGS" : "CONFIRM_EXIT_TEXT");
            view["confirm"] = Json{{"preset", "danger"},
                                   {"title", window ? question : bindings.Text("menu.exit_question")},
                                   {"text", window ? std::string() : question},
                                   {"yes", window ? yes->label : bindings.Text("menu.exit")},
                                   {"no", no ? no->label : bindings.Text("common.cancel")}};
            bindings.Forward("confirm-confirm", *yes, "press");
            if (no)
            {
                bindings.Forward("confirm-cancel", *no, "press");
                bindings.Forward("confirm", *no, "press");
            }
            else
            {
                bindings.Ignore("confirm-cancel", "");
                bindings.Ignore("confirm", "dismiss");
            }
        }
        else
            view["confirm"] = Json();
        return view;
    }
};

} // namespace

std::unique_ptr<Screen> CreateGameMenuScreen()
{
    return std::make_unique<GameMenuScreen>();
}

} // namespace storm::ui::screens
