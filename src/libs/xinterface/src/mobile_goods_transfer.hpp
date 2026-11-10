#pragma once
// The treasurer's purchase list (goodstransfer, PROGRAM/interface/GoodsTransfer.c) draws its goods
// as raw strings of a string collection over a scrolled picture and opens a good's amount window
// from the mouse position over that picture. This exports the list as rows and lets the UI open
// a good's window the way the script expects:
//   choice ANDROID_GOODS_ORDER  one row per good: label = the good's name (the collection's own
//          text), rowId = the good id, value "1" = its amount window is open, images[0] = the atlas
//          cell GOODS/<id>, context: order (amount to buy, PChar.TransferGoods), hold (in the
//          selected ship's hold, absent when the script has no cargo record); actions ["open"]
//          while the list window is active.
//   text   ANDROID_GOODS_SHIP  label = the selected ship (the script's CurCompanion string),
//          context: index (iCurCompanion, 0-based), count (ships that can take part).
// The collection's own GOODS_NAMES texts are dropped: the rows carry them. A fact the script does
// not have is named in the ship widget's unavailableReason (goods-transfer:missing=...).
// `open@<screen revision>:<good id>` opens that good's window: the engine places the interface's
// mouse position over the good's icon (GoodsIconClick) and raises the script's ShowItemsWindow.
#include "mobile_widget.hpp"
#include <algorithm>
#include <optional>

namespace storm::mobile
{

struct GoodsTransferGood
{
    std::string id, name;
    int order = 0;
    std::optional<int> hold;
};

struct GoodsTransferSource
{
    std::vector<GoodsTransferGood> goods; // in the script's order
    std::string open;                     // id of the good whose window is open, "" = none
    bool listActive = false;              // the list window takes input (no amount window)
    std::string ship;
    std::optional<int> shipIndex, shipCount;
    std::vector<std::string> missing;
};

inline void CollectGoodsTransfer(const GoodsTransferSource &source, std::vector<MobileWidget> &widgets)
{
    widgets.erase(std::remove_if(widgets.begin(), widgets.end(),
                                 [](const MobileWidget &w) { return w.node == "GOODS_NAMES"; }),
                  widgets.end());
    int index = 0;
    for (const auto &good : source.goods)
    {
        MobileWidget row{"ANDROID_GOODS_ORDER", "choice", good.name, good.id == source.open ? "1" : "0", index,
                         1000 + index, source.listActive};
        row.rowId = good.id;
        ImageRef image;
        image.source = {"atlas", "GOODS", good.id, {}, {}, -1};
        row.images.push_back(image);
        row.context["order"] = std::to_string(good.order);
        if (good.hold)
            row.context["hold"] = std::to_string(*good.hold);
        if (source.listActive)
            row.actions.push_back("open");
        widgets.push_back(std::move(row));
        ++index;
    }
    MobileWidget ship{"ANDROID_GOODS_SHIP", "text", source.ship, "", -1, 999, true};
    if (source.shipIndex)
        ship.context["index"] = std::to_string(*source.shipIndex);
    if (source.shipCount)
        ship.context["count"] = std::to_string(*source.shipCount);
    std::string missing;
    for (const auto &fact : source.missing)
        missing += (missing.empty() ? "" : ",") + fact;
    if (!missing.empty())
        ship.unavailableReason = "goods-transfer:missing=" + missing;
    widgets.push_back(std::move(ship));
}

// The good id of an `open@<revision>:<id>` action of this screen opening, or nothing.
inline std::optional<std::string> GoodsTransferOpenTarget(const std::string &value, uint64_t revision)
{
    const std::string prefix = "open@" + std::to_string(revision) + ":";
    if (value.rfind(prefix, 0) != 0 || value.size() == prefix.size())
        return std::nullopt;
    return value.substr(prefix.size());
}

// GoodsTransfer.c ShowItemsWindow maps the mouse position (mx, my) to the picture as
//   x = offset.x + (mx - 6 + 5) * scale.x,  y = offset.y + (my - 50 + 5) * scale.y
// and opens the good whose icon centre (gx, gy) satisfies
//   gx - 32 + wx <= x <= gx + 32 + wx  and  gy - 80 <= y <= gy - 16
// (wx: GetXYWindowOffset, the horizontal centring of the 800-wide layout). The point aims at the
// middle of that box.
struct GoodsIconPicture
{
    float offsetX = 0, offsetY = 0, scaleX = 1, scaleY = 1, windowX = 0;
};
struct GoodsClickPoint
{
    float x = 0, y = 0;
};
inline GoodsClickPoint GoodsIconClick(float gx, float gy, const GoodsIconPicture &p)
{
    return {(gx + p.windowX - p.offsetX) / p.scaleX + 1.0f, (gy - 48.0f - p.offsetY) / p.scaleY + 45.0f};
}
inline bool GoodsIconHit(float mx, float my, float gx, float gy, const GoodsIconPicture &p)
{
    const float x = p.offsetX + (mx - 1.0f) * p.scaleX, y = p.offsetY + (my - 45.0f) * p.scaleY;
    return x >= gx - 32 + p.windowX && x <= gx + 32 + p.windowX && y >= gy - 80 && y <= gy - 16;
}
// GetXYWindowOffset (interface.c): the layout is 600 high, at least 800 wide, centred.
inline float GoodsWindowOffsetX(float width, float height)
{
    const float screen = std::max(800.0f, width * 600.0f / std::max(1.0f, height));
    return (screen - 800.0f) / 2.0f;
}

} // namespace storm::mobile
