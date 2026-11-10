#pragma once
// The sail emblem screen (sailsgerald, PROGRAM/interface/SailsGerald.c) lists the emblem textures
// of RESOURCE/Textures/ships/gerald in a scroll image (LoadIcons: SCROLL_GERALD.picN.FileName,
// .current). This exports them and what the screen needs around them:
//   choice SCROLL_GERALD  one row per emblem: label = the file name without extension, rowId =
//          the file name, value "1" = the current one, context.texture = the texture path under
//          RESOURCE/Textures (for <ui-game-image texture=…>); actions ["select"] while selectable.
//          `select@<screen revision>:<file>` makes it current like a tap on the scroll image.
//   sails_gerald ANDROID_SAILS_GERALD  context: price (the script's price of the work), money
//          (the hero's), applied (the file on the sails now, "" = none); enabled = false;
//          unavailableReason names a fact the script did not have (sails-gerald:missing=...).
#include "mobile_widget.hpp"
#include <optional>

namespace storm::mobile
{

inline std::string SailEmblemTexture(const std::string &file)
{
    std::string base = file;
    for (const char *suffix : {".tx", ".tga"})
    {
        const std::string s = suffix;
        if (base.size() > s.size() && base.compare(base.size() - s.size(), s.size(), s) == 0)
            base.resize(base.size() - s.size());
    }
    return "ships/gerald/" + base;
}

inline void CollectSailEmblems(const std::string &node, const std::vector<std::string> &files, int current,
                               bool selectable, int order, std::vector<MobileWidget> &widgets)
{
    for (size_t i = 0; i < files.size(); ++i)
    {
        const std::string texture = SailEmblemTexture(files[i]);
        MobileWidget row{node, "choice", texture.substr(texture.rfind('/') + 1),
                         static_cast<int>(i) == current ? "1" : "0", static_cast<int>(i), order, selectable};
        row.rowId = files[i];
        row.context["texture"] = texture;
        if (selectable)
            row.actions.push_back("select");
        widgets.push_back(std::move(row));
    }
}

// The emblem index a `select@<revision>:<file>` action names, if `index` still shows that file.
inline std::optional<int> AcceptSailEmblem(const std::string &value, uint64_t revision,
                                           const std::vector<std::string> &files, int index)
{
    const std::string prefix = "select@" + std::to_string(revision) + ":";
    if (value.rfind(prefix, 0) != 0 || index < 0 || static_cast<size_t>(index) >= files.size() ||
        files[static_cast<size_t>(index)] != value.substr(prefix.size()))
        return std::nullopt;
    return index;
}

inline MobileWidget SailsGeraldMarker(std::optional<int> price, std::optional<std::string> money,
                                      std::optional<std::string> applied)
{
    MobileWidget marker{"ANDROID_SAILS_GERALD", "sails_gerald", "", "", -1, 0, false};
    std::string missing;
    const auto put = [&](const char *key, const std::optional<std::string> &value) {
        if (value)
            marker.context[key] = *value;
        else
            missing += (missing.empty() ? "" : ",") + std::string(key);
    };
    put("price", price ? std::optional<std::string>(std::to_string(*price)) : std::nullopt);
    put("money", money);
    put("applied", applied);
    if (!missing.empty())
        marker.unavailableReason = "sails-gerald:missing=" + missing;
    return marker;
}

} // namespace storm::mobile
