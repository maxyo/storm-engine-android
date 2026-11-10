// Generic auto-layout for interfaces without a ported screen (mods, rare screens):
// normalise → group → classify → choose a template (GENERIC-LAYOUT §1–7). Fills the
// pack screen <s-generic>; every block is drawn by <s-block> from shared components.
#include "controls.h"

#include <algorithm>

namespace storm::ui::screens
{

namespace
{

enum class Kind
{
    Table,
    List,
    Tabs,
    Controls,
    Text,
    Label
};

struct Block
{
    Kind kind;
    std::vector<const Widget *> widgets;
    std::vector<std::string> labels; // controls: the short text consumed as the caption of each widget
    size_t text_length = 0;
};

bool Control(const Widget &widget)
{
    return widget.type == "check" || widget.type == "edit" || widget.type == "slider" || widget.type == "stepper";
}

bool TitleNode(const Widget &widget)
{
    const std::string node = Upper(widget.node);
    return widget.type == "heading" || node == "TITLE" || node.find("CAPTION") != std::string::npos ||
           (node.size() > 6 && node.compare(node.size() - 6, 6, "_TITLE") == 0);
}

std::string Prettified(const std::string &screen)
{
    std::string out = screen;
    std::replace(out.begin(), out.end(), '_', ' ');
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

bool Numeric(const std::string &text)
{
    bool digit = false;
    for (const char c : text)
    {
        if (c >= '0' && c <= '9')
            digit = true;
        else if (c != ' ' && c != '-' && c != '+' && c != '.' && c != ',' && c != '%')
            return false;
    }
    return digit;
}

// "4000" → "4 000": plain integers get thin-space thousands, anything else stays as sent.
std::string Grouped(const std::string &text)
{
    if (text.size() < 4 || text.find_first_not_of("0123456789") != std::string::npos)
        return text;
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (i && (text.size() - i) % 3 == 0)
            out += "\xE2\x80\x89";
        out += text[i];
    }
    return out;
}

int Columns(const Widget &row)
{
    int count = static_cast<int>(row.column_headers.size());
    for (const Cell &cell : row.cells)
        count = std::max(count, cell.column + 1);
    if (row.cells.empty())
        count = std::max(count, 1 + static_cast<int>(std::count(row.label.begin(), row.label.end(), '\t')));
    return count;
}

class GenericScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-generic";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return {};
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const InterfaceModel &model = *admitted.model;

        // 1. Normalise.
        std::vector<const Widget *> shown;
        const Widget *title = nullptr;
        for (const Widget *widget : admitted.extras)
            if (!title && TitleNode(*widget) && (widget->type == "heading" || widget->type == "text"))
                title = widget;
        const bool navigation = std::any_of(admitted.extras.begin(), admitted.extras.end(),
                                            [](const Widget *w) { return w->type == "button" && !SectionOf(*w).empty(); });
        for (const Widget *widget : admitted.extras)
        {
            if (widget == title)
                continue;
            if (navigation && SameNode(widget->node, "STR_BUTTONS"))
                continue; // captions of the section buttons: the header icons replace them
            if (title && (widget->type == "text" || widget->type == "heading") && widget->label == title->label)
                continue; // the title repeated in the content
            const std::string node = Upper(widget->node);
            if (node.size() > 2 && node.compare(node.size() - 2, 2, "_2") == 0)
            {
                const Widget *original = model.Find(node.substr(0, node.size() - 2));
                if (original && original->label == widget->label && original->enabled == widget->enabled &&
                    original->value == widget->value)
                    continue;
            }
            shown.push_back(widget);
        }

        // Buttons leave the flow: they are placed by role.
        std::vector<const Widget *> buttons, flow;
        const Widget *exit = nullptr;
        std::vector<const Widget *> sections;
        for (const Widget *widget : shown)
        {
            if (widget->type != "button" && widget->type != "image" && widget->type != "save" &&
                widget->type.rfind("nation_help", 0) != 0)
            {
                flow.push_back(widget);
                continue;
            }
            switch (RoleOf(*widget))
            {
            case ButtonRole::Exit:
                if (!exit)
                    exit = widget;
                break;
            case ButtonRole::Section:
                sections.push_back(widget);
                break;
            default:
                buttons.push_back(widget);
            }
        }

        // 2. Group consecutive widgets into blocks.
        std::vector<Block> blocks;
        for (size_t i = 0; i < flow.size();)
        {
            const Widget *first = flow[i];
            size_t end = i + 1;
            if (first->type == "choice")
            {
                while (end < flow.size() && flow[end]->type == "choice" && SameNode(flow[end]->node, first->node))
                    ++end;
                Block block{Kind::List, {flow.begin() + static_cast<long>(i), flow.begin() + static_cast<long>(end)}};
                const bool columns = std::any_of(block.widgets.begin(), block.widgets.end(), [](const Widget *w) { return Columns(*w) >= 2; });
                const bool selection = std::any_of(block.widgets.begin(), block.widgets.end(), [](const Widget *w) { return w->value == "1"; });
                const bool brief = std::all_of(block.widgets.begin(), block.widgets.end(), [](const Widget *w) { return Short(w->label, 12); });
                if (columns)
                    block.kind = Kind::Table;
                else if (block.widgets.size() >= 2 && block.widgets.size() <= 4 && selection && brief)
                    block.kind = Kind::Tabs;
                blocks.push_back(std::move(block));
            }
            else if (Control(*first))
            {
                while (end < flow.size() && Control(*flow[end]))
                    ++end;
                Block block{Kind::Controls, {flow.begin() + static_cast<long>(i), flow.begin() + static_cast<long>(end)}};
                block.labels.resize(block.widgets.size());
                // A short text right before a control is its label.
                if (!blocks.empty() && blocks.back().kind == Kind::Text && blocks.back().widgets.size() == 1 &&
                    Short(blocks.back().widgets[0]->label, 30) && !(first->type == "check" && !first->exclusive))
                {
                    block.labels[0] = blocks.back().widgets[0]->label;
                    blocks.pop_back();
                }
                // Controls separated only by their captions stay one block.
                if (!blocks.empty() && blocks.back().kind == Kind::Controls)
                {
                    Block &previous = blocks.back();
                    previous.widgets.insert(previous.widgets.end(), block.widgets.begin(), block.widgets.end());
                    previous.labels.insert(previous.labels.end(), block.labels.begin(), block.labels.end());
                }
                else
                    blocks.push_back(std::move(block));
            }
            else if (first->type == "heading")
                blocks.push_back(Block{Kind::Label, {first}});
            else
            {
                while (end < flow.size() && flow[end]->type == first->type && !Control(*flow[end]) && flow[end]->type != "choice" &&
                       flow[end]->type != "heading")
                    ++end;
                Block block{Kind::Text, {flow.begin() + static_cast<long>(i), flow.begin() + static_cast<long>(end)}};
                for (const Widget *widget : block.widgets)
                    block.text_length += Length(widget->label);
                blocks.push_back(std::move(block));
            }
            i = end;
        }
        // A heading names a group only when there are at least two groups.
        if (std::count_if(blocks.begin(), blocks.end(), [](const Block &b) { return b.kind != Kind::Label; }) < 2)
            blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](const Block &b) { return b.kind == Kind::Label; }), blocks.end());

        // 3. Classify: the main block and the detail.
        int main = -1, detail = -1;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const Block &block = blocks[i];
            if ((block.kind == Kind::Table || block.kind == Kind::List) &&
                (main < 0 || block.widgets.size() > blocks[static_cast<size_t>(main)].widgets.size()))
                main = static_cast<int>(i);
            if (block.kind == Kind::Text && (detail < 0 || block.text_length > blocks[static_cast<size_t>(detail)].text_length))
                detail = static_cast<int>(i);
        }
        const auto all = [&](auto predicate) { return std::all_of(blocks.begin(), blocks.end(), predicate); };
        size_t control_rows = 0;
        for (const Block &block : blocks)
            if (block.kind == Kind::Controls)
                control_rows += block.widgets.size();

        // 4. Template.
        std::string layout;
        if (!blocks.empty() && all([](const Block &b) { return b.kind == Kind::Text || b.kind == Kind::Label; }))
            layout = "reading";
        else if (main >= 0 && detail >= 0 && blocks[static_cast<size_t>(detail)].text_length > 120)
            layout = "split";
        else if (control_rows > 0 && main < 0 &&
                 all([](const Block &b) { return b.kind == Kind::Controls || b.kind == Kind::Label || (b.kind == Kind::Text && b.text_length <= 120); }))
            // Many rows: the list on the left, the notes beside it; else one centred column.
            layout = control_rows >= 6 && std::any_of(blocks.begin(), blocks.end(), [](const Block &b) { return b.kind == Kind::Text; })
                         ? "settings-wide"
                         : "settings";
        else if (main >= 0 && all([](const Block &b) { return b.kind == Kind::Table || b.kind == Kind::List || b.kind == Kind::Tabs; }) &&
                 std::count_if(blocks.begin(), blocks.end(), [](const Block &b) { return b.kind != Kind::Tabs; }) == 1)
            layout = "list";
        else
            layout = "fallback";

        Json left = Json::array(), right = Json::array();
        const bool two = layout == "split" || layout == "settings-wide";
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const bool to_right = two && (layout == "split" ? blocks[i].kind == Kind::Text || blocks[i].kind == Kind::Label
                                                            : blocks[i].kind == Kind::Text);
            const bool on_paper = to_right || layout == "reading";
            Json view = BlockView(blocks[i], "g" + std::to_string(i), bindings, on_paper);
            view["scroll"] = two && (static_cast<int>(i) == main || static_cast<int>(i) == detail) ? true : layout == "list" && static_cast<int>(i) == main;
            (to_right ? right : left).push_back(std::move(view));
        }

        // Buttons by role (G6).
        const Widget *primary = nullptr, *danger = nullptr;
        std::vector<const Widget *> others;
        for (const Widget *button : buttons)
        {
            const ButtonRole role = RoleOf(*button);
            if (role == ButtonRole::Confirm)
            {
                if (primary)
                    others.push_back(primary);
                primary = button; // the last confirm by order wins
            }
            else if (role == ButtonRole::Danger && !danger)
                danger = button;
            else
                others.push_back(button);
        }
        std::sort(others.begin(), others.end(), [](const Widget *a, const Widget *b) { return a->order < b->order; });
        if (!primary && others.size() == 1 && !danger && others[0]->Offers("press"))
        {
            primary = others[0];
            others.clear();
        }
        // More than three non-exit buttons: two stay in the bar, the rest go under «⋯».
        std::vector<const Widget *> overflow;
        const size_t total = others.size() + (primary ? 1 : 0) + (danger ? 1 : 0);
        if (total > 3)
        {
            const size_t keep = 2 - std::min<size_t>(2, (primary ? 1 : 0) + (danger ? 1 : 0));
            overflow.assign(others.begin() + static_cast<long>(std::min(keep, others.size())), others.end());
            others.resize(std::min(keep, others.size()));
        }
        Json actions = Json::array();
        const char *surface = two || layout == "reading" ? "parchment" : "wood";
        for (const Widget *button : others)
            actions.push_back(OnSurface(ButtonView(bindings, *button, "g-b" + std::to_string(actions.size()), "secondary"), surface));
        if (primary)
            actions.push_back(OnSurface(ButtonView(bindings, *primary, "g-primary", "primary"), surface));
        Json more = Json::array();
        for (const Widget *button : overflow)
            more.push_back(ActionRow(bindings, *button, "extra-a" + std::to_string(more.size())));

        // Header: title, ← (the exit button), sections.
        std::string section;
        static const char *const own[][2] = {{"character", "hero"}, {"lscchar_all", "hero"}, {"ship", "ship"},
                                             {"questbook", "journal"}, {"tradebook", "tradebook"}, {"nationrelation", "nations"},
                                             {"items", "items"}};
        for (const auto &pair : own)
            if (!sections.empty() && model.screen == pair[0])
                section = pair[1];
        if (section.empty())
            for (const Widget *button : sections)
                more.push_back(ActionRow(bindings, *button, "extra-a" + std::to_string(more.size())));
        if (!section.empty())
        {
            // One handler for every section icon.
            const std::vector<const Widget *> nav = sections;
            bindings.On("header", "section", [&bindings, nav](const Event &event) {
                for (const Widget *button : nav)
                    if (SectionOf(*button) == event.value && button->Offers("press"))
                    {
                        bindings.Send(*button, "press");
                        return;
                    }
            });
        }
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });

        Json view = Json::object();
        view["title"] = title ? title->label : Prettified(model.screen);
        view["layout"] = layout;
        view["sections"] = section;
        view["left"] = left;
        view["right"] = right;
        view["actions"] = actions;
        view["danger"] = danger ? OnSurface(ButtonView(bindings, *danger, "g-danger", "danger"), surface) : Json();
        view["more"] = more;
        return view;
    }

  private:
    static Json OnSurface(Json button, const char *surface)
    {
        button["surface"] = surface;
        return button;
    }

    static Json BlockView(const Block &block, const std::string &id, Bindings &bindings, bool on_paper)
    {
        Json view = Json::object();
        view["id"] = id;
        view["surface"] = on_paper ? "parchment" : "wood";
        switch (block.kind)
        {
        case Kind::Table: {
            view["kind"] = "table";
            int columns = 0;
            for (const Widget *row : block.widgets)
                columns = std::max(columns, Columns(*row));
            const Widget *first = block.widgets.front();
            std::vector<bool> numeric(static_cast<size_t>(columns), true), used(static_cast<size_t>(columns), false);
            Json rows = Json::array();
            for (const Widget *row : block.widgets)
                for (int c = 0; c < columns; ++c)
                {
                    const std::string text = row->CellText(c);
                    if (!text.empty())
                    {
                        used[static_cast<size_t>(c)] = true;
                        numeric[static_cast<size_t>(c)] = numeric[static_cast<size_t>(c)] && Numeric(text);
                    }
                }
            Json heads = Json::array();
            bool head = false;
            bool grown = false;
            for (int c = 0; c < columns; ++c)
            {
                if (!used[static_cast<size_t>(c)])
                    continue;
                std::string label = static_cast<size_t>(c) < first->column_headers.size() ? first->column_headers[static_cast<size_t>(c)] : "";
                std::replace(label.begin(), label.end(), '\n', ' ');
                head = head || !label.empty();
                const bool number = numeric[static_cast<size_t>(c)];
                // The first text column takes the free width; numbers are right-aligned.
                Json column{{"label", label}, {"align", number ? "right" : "left"}};
                if (!number && !grown)
                {
                    column["w"] = 3;
                    grown = true;
                }
                else
                    column["w"] = number ? Json("88px") : Json(2);
                heads.push_back(std::move(column));
            }
            for (const Widget *row : block.widgets)
            {
                Json cells = Json::array();
                for (int c = 0; c < columns; ++c)
                    if (used[static_cast<size_t>(c)])
                        cells.push_back(numeric[static_cast<size_t>(c)] ? Json{{"text", Grouped(row->CellText(c))}, {"bold", true}}
                                                                        : Json{{"text", row->CellText(c)}});
                const bool usable = row->Offers("select") || row->Offers("activate") || row->Offers("open");
                rows.push_back(Json{{"cells", cells},
                                    {"selected", row->value == "1"},
                                    {"disabled", !row->enabled || (!usable && !row->actions.empty())},
                                    {"reason", row->unavailable_reason}});
            }
            view["columns"] = heads;
            view["rows"] = rows;
            view["head"] = head;
            const std::vector<const Widget *> widgets = block.widgets;
            view["interactive"] = std::any_of(widgets.begin(), widgets.end(), [](const Widget *w) { return w->enabled; });
            bindings.On(id, "row", [&bindings, widgets](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (index < widgets.size())
                    TapChoice(bindings, *widgets[index]);
            });
            break;
        }
        case Kind::List: {
            view["kind"] = "list";
            Json rows = Json::array();
            for (size_t i = 0; i < block.widgets.size(); ++i)
            {
                Json row = ActionRow(bindings, *block.widgets[i], id + "-" + std::to_string(i));
                // Quest titles: value "1" = completed.
                if (block.widgets[i]->Offers("open"))
                {
                    row["muted"] = block.widgets[i]->value == "1";
                    row["selected"] = false;
                }
                rows.push_back(std::move(row));
            }
            view["rows"] = rows;
            break;
        }
        case Kind::Tabs: {
            view["kind"] = "tabs";
            Json items = Json::array();
            for (const Widget *tab : block.widgets)
                items.push_back(Json{{"label", tab->label}, {"selected", tab->value == "1"}, {"disabled", !tab->Offers("select")}});
            view["items"] = items;
            const std::vector<const Widget *> widgets = block.widgets;
            bindings.On(id, "select", [&bindings, widgets](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (index < widgets.size())
                    TapChoice(bindings, *widgets[index]);
            });
            break;
        }
        case Kind::Controls: {
            view["kind"] = "controls";
            Json rows = Json::array();
            ControlRows(bindings, block.widgets, id + "-", block.labels, rows);
            view["rows"] = rows;
            break;
        }
        case Kind::Text: {
            view["kind"] = "text";
            Json paragraphs = Json::array();
            for (const Widget *widget : block.widgets)
            {
                size_t at = 0;
                while (at <= widget->label.size())
                {
                    size_t end = widget->label.find('\n', at);
                    if (end == std::string::npos)
                        end = widget->label.size();
                    if (end > at)
                        paragraphs.push_back(widget->label.substr(at, end - at));
                    at = end + 1;
                }
            }
            view["paragraphs"] = paragraphs;
            break;
        }
        case Kind::Label:
            view["kind"] = "label";
            view["text"] = block.widgets.front()->label;
            break;
        }
        return view;
    }
};

} // namespace

std::unique_ptr<Screen> CreateGenericScreen()
{
    return std::make_unique<GenericScreen>();
}

} // namespace storm::ui::screens
