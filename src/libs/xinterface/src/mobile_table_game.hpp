#pragma once
// The tavern mini-games (boal_dice, boal_cards) draw their dice, cards, coins and money lines
// with script images (CreateImage) and script strings (CreateString), not with nodes. This
// exports them, plus the script state the UI needs to know whose turn it is, as widgets:
//   image  <image name>  label = picture, images[0] = the atlas cell (only images with a picture)
//   text   <string name> label = the text (GameInterface.strings; the beta-test lines are left out)
//   table_game ANDROID_TABLE_GAME  context = the script facts below; enabled = false,
//          unavailableReason names the facts the script did not have (never guessed).
#include "mobile_widget.hpp"
#include <map>
#include <optional>

namespace storm::mobile
{

struct TableGameImage
{
    std::string node;
    int x = 0, y = 0;
    ImageRef image; // source.group / source.image name the picture
};

struct TableGameSource
{
    std::vector<TableGameImage> images;
    std::vector<std::pair<std::string, std::string>> strings;
    // Context key -> value; nullopt = the script did not provide it (a missing fact).
    std::vector<std::pair<std::string, std::optional<std::string>>> facts;
};

inline void CollectTableGame(const TableGameSource &source, std::vector<MobileWidget> &widgets)
{
    for (const auto &image : source.images)
    {
        if (image.image.source.group.empty() || image.image.source.image.empty())
            continue; // CreateImage(name, "", "") removes a picture
        MobileWidget widget{image.node, "image", image.image.source.image, "", -1, image.y * 8192 + image.x, false};
        widget.images.push_back(image.image);
        widgets.push_back(std::move(widget));
    }
    for (const auto &[name, text] : source.strings)
    {
        if (name.rfind("Beta_", 0) == 0)
            continue;
        widgets.push_back({name, "text", text, "", -1, 0, false});
    }
    MobileWidget marker{"ANDROID_TABLE_GAME", "table_game", "", "", -1, 0, false};
    std::string missing;
    for (const auto &[key, value] : source.facts)
    {
        if (value)
            marker.context[key] = *value;
        else
            missing += (missing.empty() ? "" : ",") + key;
    }
    if (!missing.empty())
        marker.unavailableReason = "table-game:missing=" + missing;
    widgets.push_back(std::move(marker));
}

} // namespace storm::mobile
