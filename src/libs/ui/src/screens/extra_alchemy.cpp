// Alchemy (interface "alchemy", mockups V2-Extra-Alchemy, V2-Extra-Alchemy-Crafting): the known
// recipes on the left; on the right the shown recipe, its components (what the hero has, what one
// batch takes), the number of batches and «Изготовить». The engine's export (ANDROID_ALCHEMY*,
// mobile_alchemy.hpp) does the rest: choosing a recipe is the scroll's own tap, crafting N batches
// fills the game's pot through its own transactions and presses its «Результат». Fills <s-alchemy>.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"ANDROID_ALCHEMY", "alchemy", true},
    {"ANDROID_ALCHEMY_RECIPE", "choice", true},
    {"TABLE_LIST", "choice"},
    {"STORECAPTION1", "text|heading"},
    {"EXIT_BTN", "button"},
    // the game's own ways (scroll arrows, pot arrows, result, amount window) stay behind the export
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"TABLE_ADD_ALL_BUTTON", "button"},
    {"TABLE_REMOVE_ALL_BUTTON", "button"},
    {"GETALL_BUTTON", "button"},
    {"QTY_*", "text|heading|edit|button|image"},
    {"ITEMS_SCROLL", "choice|image"},
    {"MAIN_CHARACTER_PICTURE", "image"},
    {"OTHER_PICTURE", "image"},
    {"BORDER_PICTURE", "image"},
};

std::string Trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \r\n\t");
    return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}

int ContextNumber(const Widget &widget, const char *key)
{
    const auto found = widget.context.find(key);
    return found == widget.context.end() ? 0 : std::atoi(found->second.c_str());
}
std::string Context(const Widget &widget, const char *key)
{
    const auto found = widget.context.find(key);
    return found == widget.context.end() ? std::string() : found->second;
}

// The item group's word under a recipe name; groups without one show none.
const char *KindKey(const std::string &group)
{
    if (group == "ammo")
        return StringKey("alchemy.kind_ammo");
    if (group == "tool")
        return StringKey("alchemy.kind_tool");
    if (group == "talisman")
        return StringKey("alchemy.kind_talisman");
    return nullptr;
}

// alchemy.c onGetAllBtnClick: 15 minutes for the first batch, 5 for every further one.
int Minutes(int batches)
{
    return batches < 1 ? 0 : 15 + 5 * (batches - 1);
}

class AlchemyScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-alchemy";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *marker = admitted.One("ANDROID_ALCHEMY");
        if (!marker->unavailable_reason.empty() && reported_ != marker->unavailable_reason)
        {
            reported_ = marker->unavailable_reason;
            bindings.Error("alchemy: " + marker->unavailable_reason);
        }
        const std::string recipe = Context(*marker, "recipe");
        const int possible = ContextNumber(*marker, "batches");
        const int yield = std::max(1, ContextNumber(*marker, "yield"));
        if (recipe != shown_)
        {
            shown_ = recipe;
            batches_ = -1; // a new recipe starts at one batch when it can be made
        }
        const int batches = std::clamp(batches_ < 0 ? std::min(1, possible) : batches_, 0, possible);

        Json view = Json::object();
        const std::string caption = Trim(admitted.Label("STORECAPTION1"));
        view["title"] = caption.empty() ? bindings.Text("alchemy.title") : caption;

        // The recipes.
        Json rows = Json::array();
        const Widget *current = nullptr;
        for (const Widget *row : admitted.All("ANDROID_ALCHEMY_RECIPE"))
        {
            const bool selected = row->value == "1";
            if (selected)
                current = row;
            const char *kind = KindKey(Context(*row, "group"));
            rows.push_back(Json{{"id", row->row_id},
                                {"name", row->label},
                                {"image", PictureOf(row->FirstImage())},
                                {"meta", kind ? bindings.Text(kind) : std::string()},
                                {"selected", selected},
                                {"disabled", !selected && !row->Offers("choose")},
                                {"reason", row->unavailable_reason},
                                {"cells", Json::array()}});
        }
        view["recipes"] = Json{{"label", bindings.Text("alchemy.recipe")}, {"rows", rows}};
        const std::vector<const Widget *> recipes = admitted.All("ANDROID_ALCHEMY_RECIPE");
        bindings.On("recipes", "row", [&bindings, recipes](const Event &event) {
            for (const Widget *row : recipes)
                if (row->row_id == event.value && row->Offers("choose"))
                    bindings.Send(*row, "choose", row->row_id);
        });

        // The shown recipe and what one batch takes.
        const std::string description = Context(*marker, "description");
        view["recipe"] = Json();
        if (current)
            view["recipe"] = Json{{"title", current->label},
                                  {"sub", "× " + std::to_string(yield) + " " + bindings.Text("alchemy.yield") + " · " +
                                              std::to_string(Minutes(std::max(1, batches))) + " " + bindings.Text("alchemy.minutes")},
                                  {"image", PictureOf(current->FirstImage())},
                                  {"info", !description.empty()}};
        const std::string title = current ? current->label : std::string();
        bindings.On("info", "", [&bindings, title, description](const Event &) { bindings.Read(title, description); });
        Json parts = Json::array();
        size_t index = 0;
        for (const Widget *row : admitted.All("TABLE_LIST"))
        {
            const int have = std::atoi(row->CellText(0).c_str()) + std::atoi(row->CellText(4).c_str());
            const Cell *name = nullptr;
            for (const Cell &cell : row->cells)
                if (cell.column == 2)
                    name = &cell;
            parts.push_back(Json{{"id", std::to_string(index++)},
                                 {"name", row->CellText(2)},
                                 {"image", PictureOf(name && !name->images.empty() ? &name->images.front() : row->FirstImage())},
                                 {"meta", Context(*row, "use") == "catalyst" ? bindings.Text("alchemy.catalyst") : std::string()},
                                 {"cells", Json::array({have > 0 ? Json(std::to_string(have)) : Json{{"text", bindings.Text("alchemy.none")}, {"tone", "danger"}},
                                                        row->CellText(3)})}});
        }
        view["parts"] = Json{{"label", bindings.Text("alchemy.component")},
                             {"cols", Json::array({Json{{"label", bindings.Text("alchemy.have")}, {"icon", "chest"}}, Json{{"label", bindings.Text("alchemy.per_batch")}, {"icon", "potion"}}})},
                             {"rows", parts}};
        bindings.Ignore("parts", "row");

        // How many batches, and the craft.
        view["q"] = Json{{"value", batches},
                         {"min", 0},
                         {"max", std::max(1, possible)},
                         {"label", bindings.Text("alchemy.batches")},
                         {"summary", possible > 0 ? std::string() : std::string("-")},
                         {"steps", Json::array({1, 10})},
                         {"all", true},
                         {"allLabel", bindings.Text("alchemy.max")},
                         {"disabled", possible == 0}};
        const auto set = [this, &bindings](const Event &event) {
            batches_ = std::max(0, std::atoi(event.value.c_str()));
            bindings.Rebuild();
        };
        bindings.On("batches", "change", set);
        bindings.On("batches", "commit", set);
        const bool can = marker->Offers("craft") && batches > 0 && !recipe.empty();
        view["primary"] = Json{{"label", bindings.Text("alchemy.craft") + (batches > 0 ? " ×" + std::to_string(batches * yield) : std::string())},
                               {"sub", batches > 0 ? std::to_string(Minutes(batches)) + " " + bindings.Text("alchemy.minutes") : std::string()},
                               {"disabled", !can},
                               {"reason", marker->unavailable_reason},
                               {"busy", bindings.Pending("primary")}};
        bindings.On("primary", "", [&bindings, marker, can, recipe, batches](const Event &) {
            if (can)
                bindings.Send(*marker, "craft", recipe + "=" + std::to_string(batches));
        });

        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    std::string shown_;    // the recipe the batches were set for
    int batches_ = -1;     // the batches the player set, -1 = not yet
    std::string reported_; // the missing facts already reported
};

} // namespace

std::unique_ptr<Screen> CreateAlchemyScreen()
{
    return std::make_unique<AlchemyScreen>();
}

} // namespace storm::ui::screens
