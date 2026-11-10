// The Caribbean Legend dynamic HUD as data: groups of texts and images that scripts describe in
// attributes, either registered with DynamicInterfaceService (UI_GROUP_REGISTER) or kept by the
// battle interfaces in their "dynamic" subtree (BattleInterface.dynamic.<group>, ...).
//
// This model is what the portable UI (src/libs/ui) will draw; the engine has no legacy renderer
// for it. Published as the JSON state channel "dynamicHud" (Snapshot::Json), like the other HUD
// channels (util/platform/android_*.hpp).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class ATTRIBUTES;

namespace storm::dynamic_hud
{

// Where a group is shown (UI_GROUP_LAND/SEA/WM/LOG of the scripts; a sum selects several).
enum Host : uint32_t
{
    kLand = 1,
    kSea = 2,
    kWorldMap = 4,
    kLog = 8, // all three game modes and over an open interface
};
inline constexpr uint32_t kAllHosts = kLand | kSea | kWorldMap | kLog;

// UI_GROUP_REQUEST (re-read on UI_GROUP_REFRESH) or UI_GROUP_FRAME (re-read every frame).
enum class Refresh : int32_t
{
    request = 0,
    frame = 1,
};

struct Point
{
    int32_t x = 0, y = 0;
};
struct Rect
{
    int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// texts.<id> and richtexts.<id>
struct Text
{
    std::string id;
    std::string text;
    std::string font;
    float scale = 1.0f;
    uint32_t color = 0xffffffff; // ARGB
    std::optional<Point> pos;    // screen pixels
    std::string align = "left";  // left, center, right
    // Other fields the scripts keep on the element (layout helpers such as base.pos.y,
    // adaptive, opacity, valign): access path -> value, passed through for the renderer.
    std::map<std::string, std::string> extra;
};

// images.<id>: a texture with a uv rectangle, or a picture of a picture group.
struct Image
{
    std::string id;
    std::string texture;
    std::optional<std::array<float, 4>> uv;
    std::string group, picture;
    uint32_t color = 0xffffffff;
    std::optional<Rect> pos; // left, top, right, bottom in screen pixels
    std::map<std::string, std::string> extra;
};

struct Group
{
    std::string name;
    uint32_t hosts = 0;
    Refresh refresh = Refresh::request;
    bool visible = true;
    int32_t layer = 0;
    std::vector<Text> texts;
    std::vector<Text> richtexts;
    std::vector<Image> images;
    // Group-level children that are script data, not drawable elements (layout, fade, ...).
    std::vector<std::string> script_data;
};

// Problems found while reading a group: wrong value formats, elements that cannot be drawn.
struct Report
{
    std::vector<std::string> errors;
    bool ok() const
    {
        return errors.empty();
    }
};

// Reads one group from its root attribute. `name`, `hosts` and `refresh` come from the caller.
Group ParseGroup(const ATTRIBUTES &root, std::string name, uint32_t hosts, Refresh refresh, Report &report);

struct Snapshot
{
    uint64_t revision = 0;
    std::vector<Group> groups;
    // {"schemaVersion":1,"revision":N,"groups":[{name,hosts:[land|sea|worldmap|log],refresh,visible,layer,
    //   texts:[...],richtexts:[...],images:[...]}]}
    [[nodiscard]] std::string Json() const;
};

} // namespace storm::dynamic_hud
