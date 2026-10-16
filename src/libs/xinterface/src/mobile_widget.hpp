#pragma once
#include "mobile_image_cache.hpp"
#include <string>
#include <vector>

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
};

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
    return "{\"handle\":" + std::to_string(image.handle) + ",\"imageRevision\":" + std::to_string(image.imageRevision) +
           ",\"width\":" + std::to_string(image.width) + ",\"height\":" + std::to_string(image.height) +
           ",\"tintArgb\":" + std::to_string(image.tintArgb) + '}';
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
        result += "],\"cells\":[";
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
