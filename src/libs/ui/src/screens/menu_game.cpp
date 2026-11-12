// Pause menu (interface "game_menu", mockups V2-Menu-Game and V2-Menu-ExitConfirm): the
// primary «Продолжить» over a 2 × 2 grid of save, load, settings and exit, centred over
// the paused scene. The game's exit question (CONFIRM_EXIT_*, or CONFIRM_WINDOW_* in some
// versions) becomes the danger C-Dialog. The one exit leads to the game's main menu (owner
// decision): the game's own exit to the main menu where it has one (CL: MB_EXITMENU), else
// its exit from the program (TEHO: MB_EXITGAME/MB_QUIT, ExitProgram), after which the host
// starts a fresh engine straight at the main menu (host action `app-exit-to-main-menu`; the
// engine runs once per process). The app itself is left by the main menu's own exit.
// Above the actions the game in progress
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
    {"MB_EXITMENU", "button"},
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

// «Продолжить» resumes through the menu's cancel (engine "back" → exitCancel → the script's
// ProcessCancelExit), the route of MB_RESUME's own ini command. The game honours it while the
// engine's resumeOnCancel fact is 1, also where it greys out the button itself (TEHO's
// game_menu.c during a boarding, where the original resumes only on Esc).
Json ResumeView(Bindings &bindings, const Widget &resume)
{
    const auto fact = resume.context.find("resumeOnCancel");
    if (fact == resume.context.end())
        bindings.Error("game_menu: MB_RESUME has no resumeOnCancel; «Продолжить» stays unavailable");
    const bool resumes = fact != resume.context.end() && fact->second == "1";
    if (resumes)
        bindings.On("resume", "", [&bindings](const Event &) { bindings.Back(nullptr); });
    else
        bindings.Ignore("resume", "");
    return Json{{"id", "resume"},
                {"label", resume.label},
                {"variant", "primary"},
                {"disabled", !resumes},
                {"reason", resume.unavailable_reason},
                {"busy", bindings.Pending("resume")}};
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
        view["resume"] = ResumeView(bindings, *admitted.One("MB_RESUME"));

        Json buttons = Json::array();
        // The game's exit to its main menu when it has one; its exit from the program is then
        // not offered (the main menu has its own).
        const Widget *toMenu = admitted.One("MB_EXITMENU");
        const Widget *exit = toMenu ? toMenu : admitted.One("MB_EXITGAME") ? admitted.One("MB_EXITGAME") : admitted.One("MB_QUIT");
        const std::pair<const char *, const Widget *> grid[] = {
            {"save", admitted.One("MB_SAVE")}, {"load", admitted.One("MB_LOAD")}, {"options", admitted.One("MB_OPTIONS")}, {"exit", exit}};
        for (const auto &[id, button] : grid)
            if (button)
                buttons.push_back(ButtonView(bindings, *button, id, "secondary"));
        // The game's label of its program exit («Выход») would promise leaving the game: «Выйти».
        if (exit && !toMenu)
            buttons.back()["label"] = bindings.Text("menu.exit");
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
            // The game's question of its own exit to the main menu is kept; its question of the
            // program exit («выйти из игры») is not what happens here, so ours replaces it.
            view["confirm"] = Json{{"preset", "danger"},
                                   {"title", toMenu ? question : bindings.Text("menu.exit_to_main_menu_question")},
                                   {"text", std::string()},
                                   {"yes", bindings.Text("menu.exit")},
                                   {"no", window && no ? no->label : bindings.Text("common.cancel")}}; // V2-Menu-ExitConfirm: «Отмена»
            if (toMenu)
                bindings.Forward("confirm-confirm", *yes, "press");
            else
                bindings.On("confirm-confirm", "", [&bindings, yes](const Event &) {
                    // Before the press: the host must know why the engine ends before it ends.
                    bindings.SendHost("app-exit-to-main-menu", "");
                    bindings.Send(*yes, "press");
                });
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
