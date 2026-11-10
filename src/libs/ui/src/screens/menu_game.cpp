// Pause menu (interface "game_menu", mockups V2-Menu-Game and V2-Menu-ExitConfirm): the
// primary «Продолжить» over a 2 × 2 grid of save, load, settings and exit, centred over
// the paused scene. The game's exit question (CONFIRM_EXIT_*, or CONFIRM_WINDOW_* in some
// versions) becomes the danger C-Dialog. Above the actions the game in progress
// «<hero> · <location> · <date>» from the engine's ANDROID_GAME text (context hero,
// location, gameDate YYYY-MM-DD). Fills the pack screen <s-game-menu>.
#include "controls.h"
#include "menu.h"

#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"MB_RESUME", "button", true},
    {"ANDROID_GAME", "text"},
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

const char *const kMonths[] = {StringKey("common.month_1"), StringKey("common.month_2"),  StringKey("common.month_3"),
                               StringKey("common.month_4"), StringKey("common.month_5"),  StringKey("common.month_6"),
                               StringKey("common.month_7"), StringKey("common.month_8"),  StringKey("common.month_9"),
                               StringKey("common.month_10"), StringKey("common.month_11"), StringKey("common.month_12")};

// «Шарль де Мор · Сен-Пьер, улицы · 1 окт. 1654»
std::string GameLine(const Bindings &bindings, const Widget *game)
{
    if (!game)
        return {};
    std::string line = game->label;
    const auto date = game->context.find("gameDate");
    if (date != game->context.end() && date->second.size() == 10)
    {
        const int month = std::atoi(date->second.c_str() + 5);
        if (month >= 1 && month <= 12)
            line += " · " + std::to_string(std::atoi(date->second.c_str() + 8)) + " " + bindings.Text(kMonths[month - 1]) + " " +
                    date->second.substr(0, 4);
    }
    return line;
}

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
        view["line"] = GameLine(bindings, admitted.One("ANDROID_GAME"));
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
                                   {"no", window && no ? no->label : bindings.Text("common.cancel")}}; // V2-Menu-ExitConfirm: «Отмена»
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
