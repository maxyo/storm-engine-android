// Quick save (interface "quick_save"): the game shows it while a quick save is written
// (interface.c QuickSaveContinue → LaunchQuickSaveMenu, closed by seadogs.c SaveGame →
// HideQuickSaveMenu). A small plate over the game: a spinner, the game's caption
// (INFO_WINDOW_CAPTION, «Быстрое сохранение») and its line (INFO_TEXT, «Идёт сохранение…»).
// Nothing to press: the game closes it when the record is written; back does nothing (a save
// must not be cut off). Fills the pack screen <s-quick-save>.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"INFO_TEXT", "text", true},
    {"INFO_WINDOW_CAPTION", "text|heading"},
    // Not offered: the game's dimming rectangle and frame (the plate is ours), its exit
    // and OK buttons (the ini keeps them out of the window: nothing to press).
    {"INFO_WINDOW_FADER", "*"},
    {"INFO_FRAME", "*"},
    {"EXIT_BTN", "*"},
    {"B_OK", "*"},
};

class QuickSaveScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-quick-save";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }
    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        bindings.Ignore("header-back", ""); // the game closes it once the record is written
        const std::string caption = OneLine(admitted.Label("INFO_WINDOW_CAPTION"));
        return Json{{"title", caption.empty() ? bindings.Text("menu.quick_save") : caption}, {"text", OneLine(admitted.Label("INFO_TEXT"))}};
    }
};

} // namespace

std::unique_ptr<Screen> CreateQuickSaveScreen()
{
    return std::make_unique<QuickSaveScreen>();
}

} // namespace storm::ui::screens
