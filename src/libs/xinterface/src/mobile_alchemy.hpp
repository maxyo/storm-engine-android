#pragma once
// Alchemy (PROGRAM/interface/alchemy.c) keeps its recipes in a scroll of pictures and makes the
// player fill a pot row by row before «Результат» crafts as many batches as the pot allows.
// This exports what the portable UI needs and one guarded action per intent:
//   choice ANDROID_ALCHEMY_RECIPE  one row per real recipe of ITEMS_SCROLL (picN with an itemId;
//          ListSize/NotUsed padding is never a row): label = the item's name, rowId = item id,
//          value "1" = the recipe shown, index = its scroll position, images[0] = the item's atlas
//          cell, context: group (the item's groupID, "" when none), yield (items per batch);
//          actions ["choose"] while the screen is ready and the row is not the shown recipe.
//   TABLE_LIST rows (the components of the shown recipe) get context itemId and use
//          (component | catalyst).
//   ANDROID_ALCHEMY (type alchemy)  context: recipe, yield, description (the recipe's text),
//          batches (what the hero's items and the pot allow together), ready; actions ["craft"]
//          while ready and batches > 0. unavailableReason names facts the script lacks
//          (alchemy:missing=…); context busy names why the screen is not ready right now.
// `choose@<screen revision>:<item>` selects that recipe in the scroll the way a tap does.
// `craft@<screen revision>:<item>=<N>` crafts N batches of the shown recipe: the pot is set to
// exactly what N batches take (N × qty of a component, qty of a catalyst) through the script's
// own row selection and transaction, then «Результат» is pressed. Anything inconsistent → nothing.
#include "mobile_widget.hpp"
#include <algorithm>
#include <climits>
#include <optional>

namespace storm::mobile
{

struct AlchemyRecipe
{
    std::string id, name, group, atlasGroup, atlasImage;
    int index = 0; // scroll position (picN → N − 1)
    int yield = 1;
};

struct AlchemyComponent
{
    std::string itemId;
    bool catalyst = false;
    int need = 0;          // per batch (catalyst: needed once)
    int hero = 0, pot = 0; // free items of the hero, items in the pot
};

struct AlchemySource
{
    std::vector<AlchemyRecipe> recipes;
    std::string current; // the recipe shown (sCurItem)
    std::string description;
    std::vector<AlchemyComponent> components; // in TABLE_LIST order
    std::string busy;                          // why the screen is not ready now, "" = ready
    std::vector<std::string> missing;
};

// Batches the hero's items and the pot allow together (alchemy.c CheckAlchemy over hero + pot).
inline int AlchemyBatches(const std::vector<AlchemyComponent> &components)
{
    if (components.empty())
        return 0;
    int batches = INT_MAX;
    bool any = false;
    for (const auto &c : components)
    {
        const int have = c.hero + c.pot;
        if (c.need <= 0)
            return 0;
        if (c.catalyst)
        {
            if (have < c.need)
                return 0;
            continue;
        }
        any = true;
        batches = std::min(batches, have / c.need);
    }
    return any ? batches : 0;
}

// What the pot must change by, per component, for exactly `batches` batches: > 0 hero → pot.
inline std::optional<std::vector<int>> AlchemyPotChanges(const std::vector<AlchemyComponent> &components, int batches)
{
    if (batches < 1 || batches > AlchemyBatches(components))
        return std::nullopt;
    std::vector<int> changes;
    for (const auto &c : components)
        changes.push_back((c.catalyst ? c.need : c.need * batches) - c.pot);
    return changes;
}

inline void CollectAlchemy(const AlchemySource &source, std::vector<MobileWidget> &widgets)
{
    const bool ready = source.busy.empty() && source.missing.empty();
    int order = 0;
    for (const auto &recipe : source.recipes)
    {
        const bool shown = recipe.id == source.current;
        MobileWidget row{"ANDROID_ALCHEMY_RECIPE", "choice", recipe.name, shown ? "1" : "0", recipe.index, 100 + order++, true};
        row.rowId = recipe.id;
        ImageRef image;
        image.source = {"atlas", recipe.atlasGroup, recipe.atlasImage, {}, {}, -1};
        row.images.push_back(image);
        row.context["group"] = recipe.group;
        row.context["yield"] = std::to_string(recipe.yield);
        if (ready && !shown)
            row.actions.push_back("choose");
        widgets.push_back(std::move(row));
    }
    size_t index = 0;
    for (auto &w : widgets)
        if (w.node == "TABLE_LIST" && w.type == "choice" && index < source.components.size())
        {
            const auto &c = source.components[index++];
            w.context["itemId"] = c.itemId;
            w.context["use"] = c.catalyst ? "catalyst" : "component";
        }
    MobileWidget marker{"ANDROID_ALCHEMY", "alchemy", "", "", -1, 99, false};
    const int batches = AlchemyBatches(source.components);
    marker.context["recipe"] = source.current;
    for (const auto &recipe : source.recipes)
        if (recipe.id == source.current)
            marker.context["yield"] = std::to_string(recipe.yield);
    marker.context["description"] = source.description;
    marker.context["batches"] = std::to_string(batches);
    marker.context["ready"] = ready ? "1" : "0";
    if (!source.busy.empty())
        marker.context["busy"] = source.busy;
    if (ready && batches > 0)
    {
        marker.enabled = true;
        marker.actions.push_back("craft");
    }
    std::string missing;
    for (const auto &fact : source.missing)
        missing += (missing.empty() ? "" : ",") + fact;
    if (!missing.empty())
        marker.unavailableReason = "alchemy:missing=" + missing;
    widgets.push_back(std::move(marker));
}

// `<verb>@<revision>:<item>[=<N>]` of this screen opening; N absent → 0.
struct AlchemyAction
{
    std::string item;
    int batches = 0;
};
inline std::optional<AlchemyAction> ParseAlchemyAction(const std::string &value, const char *verb, uint64_t revision, bool counted)
{
    const std::string prefix = std::string(verb) + "@" + std::to_string(revision) + ":";
    if (value.rfind(prefix, 0) != 0)
        return std::nullopt;
    std::string rest = value.substr(prefix.size());
    AlchemyAction action;
    if (counted)
    {
        const size_t equals = rest.rfind('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == rest.size())
            return std::nullopt;
        const std::string digits = rest.substr(equals + 1);
        if (digits.size() > 4 || digits.find_first_not_of("0123456789") != std::string::npos)
            return std::nullopt;
        action.batches = std::stoi(digits);
        rest.resize(equals);
    }
    if (rest.empty())
        return std::nullopt;
    action.item = rest;
    return action;
}

} // namespace storm::mobile
