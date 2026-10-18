#pragma once
#include "mobile_image_cache.hpp"
#include <string>
#include <vector>
#include <limits>

struct MobileCell
{
    int column = 0;
    std::string text;
    uint32_t colorArgb = 0xffffffff;
    std::vector<storm::mobile::ImageRef> images;
};

struct MobileWidget
{
    std::string node, type, label, value;
    int index = -1;
    int order = 0;
    bool enabled = true;
    std::string rowId;
    std::vector<MobileCell> cells;
    std::vector<storm::mobile::ImageRef> images;
    std::vector<std::string> actions;
    // Optional schema-2 capabilities, serialized only for their control type.
    int min = 0, max = 0, step = 1;
    bool exclusive = false;
    std::string unavailableReason;
    // Table hr.tdN.str, indexed by MobileCell::column; absent without hr.
    std::vector<std::string> columnHeaders;
};

inline bool MobileSliderPosition(const std::string &text, int minimum, int maximum, int &position)
{
    if (text.empty() || minimum < 0 || maximum < minimum) return false;
    int parsed = 0;
    for (const unsigned char ch : text)
    {
        if (ch < '0' || ch > '9' || parsed > (std::numeric_limits<int>::max() - (ch - '0')) / 10)
            return false;
        parsed = parsed * 10 + (ch - '0');
    }
    if (parsed < minimum || parsed > maximum) return false;
    position = parsed;
    return true;
}

inline bool MobileSliderSetPosition(const std::string &action, uint64_t screenRevision, int &position)
{
    const std::string prefix = "set@" + std::to_string(screenRevision) + ":";
    return action.compare(0, prefix.size(), prefix) == 0 &&
           MobileSliderPosition(action.substr(prefix.size()), 0, std::numeric_limits<int>::max(), position);
}

inline std::string MobileJsonString(const std::string &text)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
        {
            result += '\\';
            result += static_cast<char>(c);
        }
        else if (c < 32)
        {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        }
        else
            result += static_cast<char>(c);
    }
    return result + '"';
}

inline std::string MobileImageJson(const storm::mobile::ImageRef &image)
{
    std::string json = "{\"handle\":" + std::to_string(image.handle) + ",\"imageRevision\":" + std::to_string(image.imageRevision) +
           ",\"width\":" + std::to_string(image.width) + ",\"height\":" + std::to_string(image.height) +
           ",\"tintArgb\":" + std::to_string(image.tintArgb);
    const auto &source = image.source;
    if (source.kind == "atlas" && !source.group.empty() && !source.image.empty())
        json += ",\"source\":{\"kind\":\"atlas\",\"group\":" + MobileJsonString(source.group) +
                ",\"image\":" + MobileJsonString(source.image) + '}';
    else if (source.kind == "battle_icon" && !source.mode.empty() && !source.atlas.empty() && source.pic >= 0)
        json += ",\"source\":{\"kind\":\"battle_icon\",\"mode\":" + MobileJsonString(source.mode) +
                ",\"atlas\":" + MobileJsonString(source.atlas) + ",\"pic\":" + std::to_string(source.pic) + '}';
    return json + '}';
}
inline std::string MobileImagesJson(const std::vector<storm::mobile::ImageRef> &images)
{
    std::string out = "[";
    for (const auto &i : images)
    {
        if (out.size() > 1)
            out += ',';
        out += MobileImageJson(i);
    }
    return out + ']';
}
inline std::string MobileWidgetJson(const std::string &screen, const std::vector<MobileWidget> &widgets,
                                    uint64_t screenRevision = 0, uint64_t revision = 0,
                                    const std::string &language = {})
{
    std::string result = "{\"schemaVersion\":2,\"language\":" + MobileJsonString(language) +
                         ",\"revision\":" + std::to_string(revision) +
                         ",\"screenRevision\":" + std::to_string(screenRevision) +
                         ",\"screen\":" + MobileJsonString(screen) + ",\"widgets\":[";
    bool first = true;
    for (const auto &widget : widgets)
    {
        if (!first)
            result += ',';
        first = false;
        result += "{\"node\":" + MobileJsonString(widget.node) + ",\"type\":" + MobileJsonString(widget.type) +
                  ",\"label\":" + MobileJsonString(widget.label) + ",\"value\":" + MobileJsonString(widget.value) +
                  ",\"index\":" + std::to_string(widget.index) + ",\"enabled\":" + (widget.enabled ? "true" : "false") +
                  ",\"rowId\":" + MobileJsonString(widget.rowId) + ",\"images\":" + MobileImagesJson(widget.images) +
                  ",\"actions\":[";
        bool firstAction = true;
        for (const auto &a : widget.actions)
        {
            if (!firstAction)
                result += ',';
            firstAction = false;
            result += MobileJsonString(a);
        }
        result += "]";
        if (widget.type == "slider")
            result += ",\"min\":" + std::to_string(widget.min) + ",\"max\":" + std::to_string(widget.max) +
                      ",\"step\":" + std::to_string(widget.step);
        if (widget.type == "check")
            result += std::string(",\"exclusive\":") + (widget.exclusive ? "true" : "false");
        if (!widget.unavailableReason.empty())
            result += ",\"unavailableReason\":" + MobileJsonString(widget.unavailableReason);
        if (!widget.columnHeaders.empty())
        {
            result += ",\"columnHeaders\":[";
            for (size_t column = 0; column < widget.columnHeaders.size(); ++column)
            {
                if (column) result += ',';
                result += MobileJsonString(widget.columnHeaders[column]);
            }
            result += ']';
        }
        result += ",\"cells\":[";
        bool firstCell = true;
        for (const auto &c : widget.cells)
        {
            if (!firstCell)
                result += ',';
            firstCell = false;
            result += "{\"column\":" + std::to_string(c.column) + ",\"text\":" + MobileJsonString(c.text) +
                      ",\"colorArgb\":" + std::to_string(c.colorArgb) + ",\"images\":" + MobileImagesJson(c.images) +
                      '}';
        }
        result += "]}";
    }
    return result + "]}";
}
