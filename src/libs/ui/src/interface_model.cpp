#include "storm_ui/interface_model.h"

#include <nlohmann/json.hpp>

namespace storm::ui
{

namespace
{

using Json = nlohmann::json;

std::string Text(const Json &object, const char *key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

template <typename Number> Number Num(const Json &object, const char *key, Number fallback)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<Number>() : fallback;
}

bool Flag(const Json &object, const char *key, bool fallback)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

std::vector<ImageRef> Images(const Json &object)
{
    std::vector<ImageRef> out;
    const auto list = object.find("images");
    if (list == object.end() || !list->is_array())
        return out;
    for (const Json &entry : *list)
    {
        if (!entry.is_object())
            continue;
        ImageRef image;
        image.handle = Num<int64_t>(entry, "handle", 0);
        image.revision = Num<int64_t>(entry, "imageRevision", 0);
        image.width = Num<int>(entry, "width", 0);
        image.height = Num<int>(entry, "height", 0);
        image.tint_argb = Num<uint32_t>(entry, "tintArgb", 0xffffffffu);
        const auto source = entry.find("source");
        if (source != entry.end() && source->is_object())
        {
            image.kind = Text(*source, "kind");
            image.group = Text(*source, "group");
            image.image = Text(*source, "image");
            image.mode = Text(*source, "mode");
            image.atlas = Text(*source, "atlas");
            image.pic = Num<int>(*source, "pic", -1);
        }
        out.push_back(std::move(image));
    }
    return out;
}

std::vector<Cell> Cells(const Json &list)
{
    std::vector<Cell> out;
    int position = 0;
    for (const Json &entry : list)
    {
        if (!entry.is_object())
            continue;
        Cell cell;
        cell.column = Num<int>(entry, "column", position);
        cell.text = Text(entry, "text");
        cell.color_argb = Num<uint32_t>(entry, "colorArgb", 0xffffffffu);
        cell.images = Images(entry);
        out.push_back(std::move(cell));
        ++position;
    }
    return out;
}

} // namespace

bool SameNode(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

bool Widget::Offers(std::string_view verb) const
{
    if (!enabled)
        return false;
    if (!actions.empty())
    {
        for (const std::string &action : actions)
            if (action == verb)
                return true;
        return false;
    }
    // Exporters that predate the verb list.
    if (type == "button")
        return verb == "press";
    if (type == "choice")
        return verb == "select";
    if (type == "check")
        return verb == "toggle";
    if (type == "edit")
        return verb == "edit";
    if (type == "stepper")
        return verb == "left" || verb == "right";
    return false;
}

std::string Widget::CellText(int column) const
{
    for (const Cell &cell : cells)
        if (cell.column == column)
            return cell.text;
    size_t at = 0;
    for (int i = 0; i < column; ++i)
    {
        at = label.find('\t', at);
        if (at == std::string::npos)
            return {};
        ++at;
    }
    const size_t end = label.find('\t', at);
    return label.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

const ImageRef *Widget::FirstImage() const
{
    if (!images.empty())
        return &images.front();
    for (const Cell &cell : cells)
        if (!cell.images.empty())
            return &cell.images.front();
    return nullptr;
}

const Widget *InterfaceModel::Find(std::string_view node, std::string_view type) const
{
    for (const Widget &widget : widgets)
        if (SameNode(widget.node, node) && (type.empty() || widget.type == type))
            return &widget;
    return nullptr;
}

bool ParseInterfaceModel(std::string_view json, InterfaceModel &out, std::string *error)
{
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    const auto widgets = root.is_object() ? root.find("widgets") : root.end();
    if (!root.is_object() || widgets == root.end() || !widgets->is_array())
    {
        if (error)
            *error = "interface model: not an object with widgets";
        return false;
    }
    out = InterfaceModel();
    out.screen = Text(root, "screen");
    out.language = Text(root, "language");
    out.opening = Num<uint64_t>(root, "screenRevision", 0);
    out.revision = Num<uint64_t>(root, "revision", 0);
    for (const Json &entry : *widgets)
    {
        if (!entry.is_object())
            continue;
        Widget widget;
        widget.node = Text(entry, "node");
        widget.type = Text(entry, "type");
        widget.label = Text(entry, "label");
        widget.value = Text(entry, "value");
        widget.index = Num<int>(entry, "index", -1);
        widget.order = static_cast<int>(out.widgets.size()); // the engine sends them sorted
        widget.enabled = Flag(entry, "enabled", true);
        widget.row_id = Text(entry, "rowId");
        widget.images = Images(entry);
        if (const auto cells = entry.find("cells"); cells != entry.end() && cells->is_array())
            widget.cells = Cells(*cells);
        if (const auto actions = entry.find("actions"); actions != entry.end() && actions->is_array())
            for (const Json &verb : *actions)
                if (verb.is_string())
                    widget.actions.push_back(verb.get<std::string>());
        widget.min = Num<int>(entry, "min", 0);
        widget.max = Num<int>(entry, "max", 0);
        widget.step = Num<int>(entry, "step", 1);
        widget.exclusive = Flag(entry, "exclusive", false);
        widget.unavailable_reason = Text(entry, "unavailableReason");
        if (const auto headers = entry.find("columnHeaders"); headers != entry.end() && headers->is_array())
            for (const Json &header : *headers)
                widget.column_headers.push_back(header.is_string() ? header.get<std::string>() : std::string());
        if (const auto cells = entry.find("headerCells"); cells != entry.end() && cells->is_array())
            widget.header_cells = Cells(*cells);
        if (const auto context = entry.find("context"); context != entry.end() && context->is_object())
            for (const auto &item : context->items())
                if (item.value().is_string())
                    widget.context[item.key()] = item.value().get<std::string>();
        if (const auto color = entry.find("textColorArgb"); color != entry.end() && color->is_number())
            widget.text_color_argb = color->get<uint32_t>();
        out.widgets.push_back(std::move(widget));
    }
    return true;
}

} // namespace storm::ui
