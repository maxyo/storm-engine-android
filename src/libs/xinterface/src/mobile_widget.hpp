#pragma once
#include <string>
#include <vector>

struct MobileWidget
{
    std::string node, type, label, value;
    int index = -1;
    int order = 0;
    bool enabled = true;
};

inline std::string MobileJsonString(const std::string &text)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}

inline std::string MobileWidgetJson(const std::string &screen, const std::vector<MobileWidget> &widgets)
{
    std::string result = "{\"screen\":" + MobileJsonString(screen) + ",\"widgets\":[";
    bool first = true;
    for (const auto &widget : widgets)
    {
        if (!first) result += ',';
        first = false;
        result += "{\"node\":" + MobileJsonString(widget.node) + ",\"type\":" + MobileJsonString(widget.type) +
                  ",\"label\":" + MobileJsonString(widget.label) + ",\"value\":" + MobileJsonString(widget.value) +
                  ",\"index\":" + std::to_string(widget.index) + ",\"enabled\":" + (widget.enabled ? "true" : "false") + '}';
    }
    return result + "]}";
}
