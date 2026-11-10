#include "dynamic_hud_model.hpp"

#include "attributes.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace storm::dynamic_hud
{
namespace
{
std::string Value(const ATTRIBUTES &a)
{
    const char *value = a.GetThisAttr();
    return value ? value : "";
}

std::string Where(const std::string &group, const std::string &path)
{
    return "dynamic HUD group '" + group + "' " + path;
}

std::vector<std::string> Split(const std::string &text)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true)
    {
        const size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
            return parts;
        start = comma + 1;
    }
}

bool ToInt(const std::string &text, int64_t &out)
{
    const char *begin = text.c_str();
    while (*begin == ' ')
        ++begin;
    char *end = nullptr;
    errno = 0;
    out = std::strtoll(begin, &end, 10);
    // Script floats assigned to an int field arrive as "12.000000": accept their integer part.
    if (end && *end == '.')
        std::strtod(begin, &end);
    while (end && *end == ' ')
        ++end;
    return end != begin && end && *end == '\0' && errno == 0;
}

bool ToFloat(const std::string &text, float &out)
{
    const char *begin = text.c_str();
    char *end = nullptr;
    out = std::strtof(begin, &end);
    while (end && *end == ' ')
        ++end;
    return end != begin && end && *end == '\0';
}

template <size_t N> bool Ints(const std::string &text, std::array<int32_t, N> &out)
{
    const auto parts = Split(text);
    if (parts.size() != N)
        return false;
    for (size_t i = 0; i < N; ++i)
    {
        int64_t v;
        if (!ToInt(parts[i], v))
            return false;
        out[i] = static_cast<int32_t>(v);
    }
    return true;
}

class Reader
{
  public:
    Reader(const std::string &group, Report &report) : group_(group), report_(report)
    {
    }

    void Error(const std::string &path, const std::string &what)
    {
        report_.errors.push_back(Where(group_, path) + ": " + what);
    }

    void Color(const ATTRIBUTES &a, const std::string &path, uint32_t &out)
    {
        int64_t v;
        if (ToInt(Value(a), v))
            out = static_cast<uint32_t>(v);
        else
            Error(path, "color '" + Value(a) + "' is not an integer ARGB");
    }

    void Float(const ATTRIBUTES &a, const std::string &path, float &out)
    {
        if (!ToFloat(Value(a), out))
            Error(path, "'" + Value(a) + "' is not a number");
    }

    // Children of an element that are not modelled fields, flattened to dotted paths.
    void Extra(const ATTRIBUTES &a, const std::string &prefix, std::map<std::string, std::string> &extra)
    {
        if (a.HasValue() || a.GetAttributesNum() == 0)
            extra[prefix] = Value(a);
        for (size_t i = 0; i < a.GetAttributesNum(); ++i)
        {
            const auto &child = *a.GetAttributeClass(static_cast<uint32_t>(i));
            Extra(child, prefix + "." + child.GetThisName(), extra);
        }
    }

    Text ReadText(const ATTRIBUTES &element, const std::string &path)
    {
        Text text;
        text.id = element.GetThisName();
        for (size_t i = 0; i < element.GetAttributesNum(); ++i)
        {
            const auto &field = *element.GetAttributeClass(static_cast<uint32_t>(i));
            const std::string key = field.GetThisName();
            const std::string at = path + "." + key;
            if (key == "text")
                text.text = Value(field);
            else if (key == "font")
                text.font = Value(field);
            else if (key == "scale")
                Float(field, at, text.scale);
            else if (key == "color")
                Color(field, at, text.color);
            else if (key == "align")
            {
                text.align = Value(field);
                if (text.align != "left" && text.align != "center" && text.align != "right")
                    Error(at, "align '" + text.align + "' is not left, center or right");
            }
            else if (key == "pos" && field.GetAttributesNum() == 0)
            {
                std::array<int32_t, 2> xy{};
                if (Ints(Value(field), xy))
                    text.pos = Point{xy[0], xy[1]};
                else
                    Error(at, "pos '" + Value(field) + "' is not \"x,y\"");
            }
            else
                Extra(field, key, text.extra);
        }
        return text;
    }

    Image ReadImage(const ATTRIBUTES &element, const std::string &path)
    {
        Image image;
        image.id = element.GetThisName();
        for (size_t i = 0; i < element.GetAttributesNum(); ++i)
        {
            const auto &field = *element.GetAttributeClass(static_cast<uint32_t>(i));
            const std::string key = field.GetThisName();
            const std::string at = path + "." + key;
            if (key == "texture")
                image.texture = Value(field);
            else if (key == "group")
                image.group = Value(field);
            else if (key == "picture")
                image.picture = Value(field);
            else if (key == "color")
                Color(field, at, image.color);
            else if (key == "uv")
            {
                const auto parts = Split(Value(field));
                std::array<float, 4> uv{};
                bool ok = parts.size() == 4;
                for (size_t k = 0; ok && k < 4; ++k)
                    ok = ToFloat(parts[k], uv[k]);
                if (ok)
                    image.uv = uv;
                else
                    Error(at, "uv '" + Value(field) + "' is not \"u0,v0,u1,v1\"");
            }
            else if (key == "pos")
            {
                std::array<int32_t, 4> r{};
                if (Ints(Value(field), r))
                    image.pos = Rect{r[0], r[1], r[2], r[3]};
                else
                    Error(at, "pos '" + Value(field) + "' is not \"left,top,right,bottom\"");
            }
            else
                Extra(field, key, image.extra);
        }
        if (image.texture.empty() && image.group.empty())
            Error(path, "image has neither a texture nor a picture group");
        return image;
    }

  private:
    const std::string &group_;
    Report &report_;
};

const char *HostName(uint32_t bit)
{
    switch (bit)
    {
    case kLand:
        return "land";
    case kSea:
        return "sea";
    case kWorldMap:
        return "worldmap";
    default:
        return "log";
    }
}

nlohmann::json TextJson(const Text &t)
{
    nlohmann::json j{{"id", t.id}, {"text", t.text}, {"font", t.font}, {"scale", t.scale}, {"color", t.color},
                     {"align", t.align}};
    if (t.pos)
        j["pos"] = {t.pos->x, t.pos->y};
    if (!t.extra.empty())
        j["extra"] = t.extra;
    return j;
}
} // namespace

Group ParseGroup(const ATTRIBUTES &root, std::string name, uint32_t hosts, Refresh refresh, Report &report)
{
    Group group;
    group.name = std::move(name);
    group.hosts = hosts;
    group.refresh = refresh;
    Reader reader(group.name, report);
    for (size_t i = 0; i < root.GetAttributesNum(); ++i)
    {
        const auto &child = *root.GetAttributeClass(static_cast<uint32_t>(i));
        const std::string key = child.GetThisName();
        if (key == "visible")
        {
            const std::string v = Value(child);
            if (v == "1" || v == "true")
                group.visible = true;
            else if (v == "0" || v == "false")
                group.visible = false;
            else
                reader.Error(key, "visible '" + v + "' is not a boolean");
        }
        else if (key == "layer")
        {
            int64_t layer;
            if (ToInt(Value(child), layer))
                group.layer = static_cast<int32_t>(layer);
            else
                reader.Error(key, "layer '" + Value(child) + "' is not an integer");
        }
        else if (key == "texts" || key == "richtexts" || key == "images")
        {
            for (size_t k = 0; k < child.GetAttributesNum(); ++k)
            {
                const auto &element = *child.GetAttributeClass(static_cast<uint32_t>(k));
                const std::string path = key + "." + element.GetThisName();
                if (key == "images")
                    group.images.push_back(reader.ReadImage(element, path));
                else
                    (key == "texts" ? group.texts : group.richtexts).push_back(reader.ReadText(element, path));
            }
        }
        else
            group.script_data.push_back(key);
    }
    return group;
}

std::string Snapshot::Json() const
{
    nlohmann::json out{{"schemaVersion", 1}, {"revision", revision}, {"groups", nlohmann::json::array()}};
    for (const auto &g : groups)
    {
        nlohmann::json hosts = nlohmann::json::array();
        for (uint32_t bit : {kLand, kSea, kWorldMap, kLog})
            if (g.hosts & bit)
                hosts.push_back(HostName(bit));
        nlohmann::json group{{"name", g.name},
                             {"hosts", hosts},
                             {"refresh", g.refresh == Refresh::frame ? "frame" : "request"},
                             {"visible", g.visible},
                             {"layer", g.layer},
                             {"texts", nlohmann::json::array()},
                             {"richtexts", nlohmann::json::array()},
                             {"images", nlohmann::json::array()}};
        for (const auto &t : g.texts)
            group["texts"].push_back(TextJson(t));
        for (const auto &t : g.richtexts)
            group["richtexts"].push_back(TextJson(t));
        for (const auto &i : g.images)
        {
            nlohmann::json j{{"id", i.id}, {"color", i.color}};
            if (!i.texture.empty())
                j["texture"] = i.texture;
            if (!i.group.empty())
                j["group"] = i.group, j["picture"] = i.picture;
            if (i.uv)
                j["uv"] = *i.uv;
            if (i.pos)
                j["pos"] = {i.pos->left, i.pos->top, i.pos->right, i.pos->bottom};
            if (!i.extra.empty())
                j["extra"] = i.extra;
            group["images"].push_back(std::move(j));
        }
        out["groups"].push_back(std::move(group));
    }
    return out.dump();
}

} // namespace storm::dynamic_hud
