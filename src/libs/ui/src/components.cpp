#include "components.h"

#include <RmlUi/Core/ElementText.h>

#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>
#include <optional>
#include <functional>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace storm::ui
{

namespace
{

std::string Kebab(const std::string &camel)
{
    std::string out;
    for (const char c : camel)
    {
        if (c >= 'A' && c <= 'Z')
        {
            out += '-';
            out += static_cast<char>(c - 'A' + 'a');
        }
        else
            out += c;
    }
    return out;
}

std::unordered_map<std::string, BehaviorFactory> &Factories()
{
    static std::unordered_map<std::string, BehaviorFactory> factories;
    return factories;
}

Json ParseAttribute(const PropSpec &prop, const Rml::Variant &variant)
{
    const std::string text = variant.Get<Rml::String>();
    switch (prop.type)
    {
    case PropSpec::Type::Bool:
        return !(text == "false" || text == "0" || text == "no");
    case PropSpec::Type::Int:
    case PropSpec::Type::Number:
        if (text.empty())
            return Json();
        return std::strtod(text.c_str(), nullptr);
    case PropSpec::Type::Json: {
        Json parsed = Json::parse(text, nullptr, false);
        if (parsed.is_discarded())
            return text;
        return parsed;
    }
    default:
        return text;
    }
}

Rml::Element *FindSlot(Rml::Element *root);

} // namespace

Rml::Element *ComponentSlot(ComponentElement &component)
{
    return FindSlot(&component);
}

struct RmlNode
{
    bool text = false;
    size_t begin = 0, end = 0;             // the node's whole text in the render
    size_t inner_begin = 0, inner_end = 0; // between its tags (elements)
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attributes; // raw, as written
    std::vector<RmlNode> children;
};

namespace
{

// The slot of a component is the first descendant marked with the `slot` attribute that
// does not belong to a nested component.
Rml::Element *FindSlot(Rml::Element *root)
{
    for (int i = 0; i < root->GetNumChildren(); ++i)
    {
        Rml::Element *child = root->GetChild(i);
        if (child->HasAttribute("slot"))
            return child;
        if (dynamic_cast<ComponentElement *>(child))
            continue;
        if (Rml::Element *found = FindSlot(child))
            return found;
    }
    return nullptr;
}

// "height: 35%; left: 12dp" → {height: "35%", left: "12dp"}
std::unordered_map<std::string, std::string> ParseStyle(const std::string &style)
{
    std::unordered_map<std::string, std::string> out;
    size_t at = 0;
    while (at < style.size())
    {
        size_t end = style.find(';', at);
        if (end == std::string::npos)
            end = style.size();
        const size_t colon = style.find(':', at);
        if (colon != std::string::npos && colon < end)
        {
            const auto trim = [](std::string text) {
                const size_t first = text.find_first_not_of(" \t\n"), last = text.find_last_not_of(" \t\n");
                return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
            };
            out[trim(style.substr(at, colon - at))] = trim(style.substr(colon + 1, end - colon - 1));
        }
        at = end + 1;
    }
    return out;
}

bool ParseLength(const std::string &text, Rml::Property &out)
{
    char *end = nullptr;
    const float number = std::strtof(text.c_str(), &end);
    if (end == text.c_str())
        return false;
    const std::string unit = end;
    if (unit == "%")
        out = Rml::Property(number, Rml::Unit::PERCENT);
    else if (unit == "dp")
        out = Rml::Property(number, Rml::Unit::DP);
    else if (unit == "px")
        out = Rml::Property(number, Rml::Unit::PX);
    else
        return false;
    return true;
}

// An element marked data-tween="seconds" glides to changed inline lengths (fill levels,
// knob positions) instead of jumping.
void TweenStyle(Rml::Element *element, const std::string &before, const std::string &after)
{
    const float seconds = element->GetAttribute<float>("data-tween", 0.0f);
    if (seconds <= 0.0f)
        return;
    const auto old_values = ParseStyle(before);
    for (const auto &entry : ParseStyle(after))
    {
        const auto old_value = old_values.find(entry.first);
        Rml::Property from, to;
        if (old_value == old_values.end() || old_value->second == entry.second || !ParseLength(old_value->second, from) ||
            !ParseLength(entry.second, to) || from.unit != to.unit)
            continue;
        element->Animate(entry.first, to, seconds, Rml::Tween(Rml::Tween::Cubic, Rml::Tween::Out), 1, true, 0.0f, &from);
    }
}

void MorphChildren(Rml::Element *live, Rml::Element *fresh);

// Makes `live` equal to the freshly rendered `fresh` while keeping the element itself,
// so that running animations, pressed parts and scroll positions survive a prop change.
void SyncElement(Rml::Element *live, Rml::Element *fresh)
{
    if (auto *text = rmlui_dynamic_cast<Rml::ElementText *>(live))
    {
        auto *fresh_text = rmlui_dynamic_cast<Rml::ElementText *>(fresh);
        if (fresh_text && text->GetText() != fresh_text->GetText())
            text->SetText(fresh_text->GetText());
        return;
    }
    std::vector<std::string> stale;
    for (const auto &entry : live->GetAttributes())
        // A component's own layout boundary (constants.layoutBoundary) is not its parent's template's to drop.
        if (!fresh->HasAttribute(entry.first) && !(entry.first == "rmlui-layout-boundary" && dynamic_cast<ComponentElement *>(live)))
            stale.push_back(entry.first);
    for (const std::string &name : stale)
    {
        if (name == "style")
            for (const auto &old : ParseStyle(live->GetAttribute<Rml::String>("style", "")))
                live->RemoveProperty(old.first);
        live->RemoveAttribute(name);
    }
    const Rml::ElementAttributes wanted = fresh->GetAttributes();
    for (const auto &entry : wanted)
    {
        const Rml::String value = entry.second.Get<Rml::String>();
        const bool had = live->HasAttribute(entry.first);
        const Rml::String current = live->GetAttribute<Rml::String>(entry.first, "");
        if (had && current == value)
            continue;
        if (entry.first == "style" && had)
        {
            // The attribute only adds properties: drop the ones the new text no longer has.
            const auto kept = ParseStyle(value);
            for (const auto &old : ParseStyle(current))
                if (!kept.count(old.first))
                    live->RemoveProperty(old.first);
        }
        live->SetAttribute(entry.first, value);
        if (entry.first == "style" && had)
            TweenStyle(live, current, value);
    }
    if (live->HasAttribute("slot"))
        return; // the component's own children live here
    if (live->GetTagName().rfind("ui-", 0) == 0 && live->GetTagName() != "ui-wheel")
        return; // code-drawn elements own their children (ui-clamp keeps its cut text); the wheel only places the template's
    if (auto *component = dynamic_cast<ComponentElement *>(live))
    {
        // A nested component rebuilds itself from its attributes; only content passed
        // into its slot comes from this template.
        component->SyncSlot(fresh);
        return;
    }
    MorphChildren(live, fresh);
}

// Children with a data-key (a list whose items come and go, like the HUD's log lines) keep
// their element by key, not by position: a new item gets a new element, so its entrance
// animation plays on it and a leaving item keeps animating out instead of taking over the
// text of its neighbour.
bool KeyedChildren(const std::vector<Rml::ElementPtr> &wanted)
{
    return !wanted.empty() && std::all_of(wanted.begin(), wanted.end(), [](const Rml::ElementPtr &child) { return child->HasAttribute("data-key"); });
}

void MorphKeyed(Rml::Element *live, std::vector<Rml::ElementPtr> &wanted, const std::vector<Rml::Element *> &current_children)
{
    std::map<std::string, Rml::Element *> by_key;
    for (Rml::Element *child : current_children)
        if (child->HasAttribute("data-key"))
            by_key.emplace(child->GetAttribute<Rml::String>("data-key", ""), child);
    std::set<Rml::Element *> kept;
    Rml::Element *after = nullptr; // the element the next wanted child follows
    for (Rml::ElementPtr &child : wanted)
    {
        const auto found = by_key.find(child->GetAttribute<Rml::String>("data-key", ""));
        Rml::Element *current = found != by_key.end() && found->second->GetTagName() == child->GetTagName() ? found->second : nullptr;
        if (current)
        {
            SyncElement(current, child.get());
            // Out of order (a reordered list): move it after its predecessor.
            Rml::Element *expected = after ? after->GetNextSibling() : live->GetFirstChild();
            if (expected != current)
                current = live->InsertBefore(live->RemoveChild(current), expected);
        }
        else
        {
            Rml::Element *before = after ? after->GetNextSibling() : live->GetFirstChild();
            current = before ? live->InsertBefore(std::move(child), before) : live->AppendChild(std::move(child));
        }
        kept.insert(current);
        after = current;
    }
    for (Rml::Element *child : current_children)
        if (!kept.count(child))
            live->RemoveChild(child);
}

void MorphChildren(Rml::Element *live, Rml::Element *fresh)
{
    std::vector<Rml::ElementPtr> wanted;
    while (fresh->GetNumChildren() > 0)
        wanted.push_back(fresh->RemoveChild(fresh->GetFirstChild()));
    // Children the runtime added itself (the scroll-edge fade) are not the template's: they
    // stay. Dropping them on every rebuild made them come back as new elements, which laid
    // the whole document out a second time on every model change.
    std::vector<Rml::Element *> current_children;
    for (int i = 0; i < live->GetNumChildren(); ++i)
        if (!live->GetChild(i)->IsClassSet("scroll-fade"))
            current_children.push_back(live->GetChild(i));
    if (KeyedChildren(wanted))
        return MorphKeyed(live, wanted, current_children);
    for (size_t i = 0; i < wanted.size(); ++i)
    {
        Rml::Element *current = i < current_children.size() ? current_children[i] : nullptr;
        // A component that takes another id is another component (a dialog with a body
        // slot after one without): build it afresh, its slot content is set only then.
        // A slot holds the component's content and is never morphed from a plain element
        // (or back): the old element's children must not end up in the slot.
        const bool same = current && current->GetTagName() == wanted[i]->GetTagName() &&
                          current->HasAttribute("slot") == wanted[i]->HasAttribute("slot") &&
                          (!dynamic_cast<ComponentElement *>(current) || current->GetId() == wanted[i]->GetAttribute<Rml::String>("id", ""));
        if (same)
            SyncElement(current, wanted[i].get());
        else if (current)
            live->ReplaceChild(std::move(wanted[i]), current);
        else
            live->AppendChild(std::move(wanted[i]));
    }
    for (size_t i = wanted.size(); i < current_children.size(); ++i)
        live->RemoveChild(current_children[i]);
}

// --- the rendered template's own tree ---------------------------------------------------
// A template renders to RML text. Parsing all of it into RmlUi elements to compare them with
// the live ones (the morph above) instanced every element of the component on every change
// of one value: the store's list re-made all its rows for a new amount. RmlNode is the same
// text as spans of a light tree, compared with the previous render's: an element whose text
// did not change is left alone, a changed one is patched attribute by attribute, and RmlUi
// elements are instanced only for what is new.

// Parses what the templates write (elements, attributes in quotes, text, comments); anything
// else is refused and the component falls back to the whole morph.
bool ParseRml(std::string_view rml, size_t &at, std::vector<RmlNode> &out, const std::string &closing)
{
    while (at < rml.size())
    {
        if (rml[at] != '<')
        {
            const size_t end = std::min(rml.find('<', at), rml.size());
            const std::string_view run = rml.substr(at, end - at);
            // RmlUi makes no element of text that is only white space (Factory::InstanceElementText).
            if (!std::all_of(run.begin(), run.end(), [](char c) { return Rml::StringUtilities::IsWhitespace(c); }))
            {
                RmlNode node;
                node.text = true;
                node.begin = at;
                node.end = end;
                out.push_back(std::move(node));
            }
            at = end;
            continue;
        }
        if (rml.compare(at, 4, "<!--") == 0)
        {
            const size_t end = rml.find("-->", at);
            if (end == std::string_view::npos)
                return false;
            at = end + 3;
            continue;
        }
        if (rml.compare(at, 2, "</") == 0)
        {
            const size_t end = rml.find('>', at);
            if (end == std::string_view::npos || closing.empty() || rml.substr(at + 2, end - at - 2) != closing)
                return false;
            at = end + 1;
            return true;
        }
        if (at + 1 >= rml.size() || !std::isalpha(static_cast<unsigned char>(rml[at + 1])))
            return false; // <! <? and the like
        RmlNode node;
        node.begin = at;
        size_t i = at + 1;
        while (i < rml.size() && (std::isalnum(static_cast<unsigned char>(rml[i])) || rml[i] == '-' || rml[i] == '_'))
            ++i;
        node.tag = std::string(rml.substr(at + 1, i - at - 1));
        bool self_closing = false;
        while (true)
        {
            while (i < rml.size() && Rml::StringUtilities::IsWhitespace(rml[i]))
                ++i;
            if (i >= rml.size())
                return false;
            if (rml[i] == '>')
            {
                ++i;
                break;
            }
            if (rml.compare(i, 2, "/>") == 0)
            {
                i += 2;
                self_closing = true;
                break;
            }
            const size_t name_begin = i;
            while (i < rml.size() && rml[i] != '=' && rml[i] != '>' && rml[i] != '/' && !Rml::StringUtilities::IsWhitespace(rml[i]))
                ++i;
            std::string name(rml.substr(name_begin, i - name_begin));
            if (name.empty() || i >= rml.size() || rml[i] != '=')
                return false; // attributes without a value are not written by the templates
            ++i;
            if (i >= rml.size() || (rml[i] != '"' && rml[i] != '\''))
                return false;
            const char quote = rml[i];
            const size_t value_end = rml.find(quote, i + 1);
            if (value_end == std::string_view::npos)
                return false;
            node.attributes.emplace_back(std::move(name), std::string(rml.substr(i + 1, value_end - i - 1)));
            i = value_end + 1;
        }
        node.inner_begin = node.inner_end = i;
        at = i;
        if (!self_closing)
        {
            if (!ParseRml(rml, at, node.children, node.tag))
                return false;
            node.inner_end = rml.rfind("</", at - 1);
        }
        node.end = at;
        out.push_back(std::move(node));
    }
    return closing.empty();
}

std::string_view Span(std::string_view rml, const RmlNode &node)
{
    return rml.substr(node.begin, node.end - node.begin);
}

// RmlUi elements for a piece of the render (one node, or the run of a node list).
Rml::ElementPtr Instance(Rml::Element *live, std::string_view rml)
{
    Rml::ElementPtr fresh = live->GetOwnerDocument()->CreateElement("div");
    fresh->SetInnerRML(Rml::String(rml));
    return fresh;
}

struct PatchStats
{
    int touched = 0;    // elements given another attribute or text, made, or removed
    int fallbacks = 0;  // levels morphed whole (a keyed list, a live tree that is not the last render's)
};

void PatchChildren(Rml::Element *live, std::string_view before, const std::vector<RmlNode> &old_nodes, std::string_view after,
                   const std::vector<RmlNode> &new_nodes, PatchStats &stats);

// `live` was made from `old_node`; makes it what `new_node` describes. Same rules as SyncElement.
void PatchElement(Rml::Element *live, std::string_view before, const RmlNode &old_node, std::string_view after, const RmlNode &new_node,
                  PatchStats &stats)
{
    if (new_node.text)
    {
        auto *text = rmlui_dynamic_cast<Rml::ElementText *>(live);
        const Rml::String value = Rml::StringUtilities::DecodeRml(Rml::String(Span(after, new_node)));
        if (text && text->GetText() != value)
        {
            text->SetText(value);
            ++stats.touched;
        }
        return;
    }
    std::map<std::string, Rml::String> wanted;
    for (const auto &[name, value] : new_node.attributes)
        wanted[name] = Rml::StringUtilities::DecodeRml(value);
    bool touched = false;
    std::vector<std::string> stale;
    for (const auto &entry : live->GetAttributes())
        if (!wanted.count(entry.first) && !(entry.first == "rmlui-layout-boundary" && dynamic_cast<ComponentElement *>(live)) &&
            std::any_of(old_node.attributes.begin(), old_node.attributes.end(), [&](const auto &old) { return old.first == entry.first; }))
            stale.push_back(entry.first); // only what the template set: runtime attributes stay, as with an unchanged render
    for (const std::string &name : stale)
    {
        if (name == "style")
            for (const auto &old : ParseStyle(live->GetAttribute<Rml::String>("style", "")))
                live->RemoveProperty(old.first);
        live->RemoveAttribute(name);
        touched = true;
    }
    for (const auto &[name, value] : wanted)
    {
        const bool had = live->HasAttribute(name);
        const Rml::String current = live->GetAttribute<Rml::String>(name, "");
        if (had && current == value)
            continue;
        if (name == "style" && had)
        {
            const auto kept = ParseStyle(value);
            for (const auto &old : ParseStyle(current))
                if (!kept.count(old.first))
                    live->RemoveProperty(old.first);
        }
        live->SetAttribute(name, value);
        if (name == "style" && had)
            TweenStyle(live, current, value);
        touched = true;
    }
    if (touched)
        ++stats.touched;
    if (live->HasAttribute("slot"))
        return; // the component's own children live here
    if (live->GetTagName().rfind("ui-", 0) == 0 && live->GetTagName() != "ui-wheel")
        return; // code-drawn elements own their children
    const std::string_view old_inner = before.substr(old_node.inner_begin, old_node.inner_end - old_node.inner_begin);
    const std::string_view new_inner = after.substr(new_node.inner_begin, new_node.inner_end - new_node.inner_begin);
    if (auto *component = dynamic_cast<ComponentElement *>(live))
    {
        // A nested component rebuilds itself from its attributes; only content passed into
        // its slot comes from this template.
        if (old_inner != new_inner)
        {
            Rml::ElementPtr fresh = Instance(live, new_inner);
            component->SyncSlot(fresh.get());
            ++stats.touched;
        }
        return;
    }
    if (old_inner != new_inner)
        PatchChildren(live, before, old_node.children, after, new_node.children, stats);
}

void PatchChildren(Rml::Element *live, std::string_view before, const std::vector<RmlNode> &old_nodes, std::string_view after,
                   const std::vector<RmlNode> &new_nodes, PatchStats &stats)
{
    std::vector<Rml::Element *> current;
    for (int i = 0; i < live->GetNumChildren(); ++i)
        if (!live->GetChild(i)->IsClassSet("scroll-fade")) // the runtime's own (MorphChildren)
            current.push_back(live->GetChild(i));
    const bool keyed = !new_nodes.empty() && std::all_of(new_nodes.begin(), new_nodes.end(), [](const RmlNode &node) {
        return std::any_of(node.attributes.begin(), node.attributes.end(), [](const auto &a) { return a.first == "data-key"; });
    });
    if (keyed || current.size() != old_nodes.size())
    {
        // A keyed list, or a live level that is not the last render (code added or removed
        // children): the whole level is morphed as before.
        ++stats.fallbacks;
        const std::string_view run =
            new_nodes.empty() ? std::string_view() : after.substr(new_nodes.front().begin, new_nodes.back().end - new_nodes.front().begin);
        Rml::ElementPtr fresh = Instance(live, run);
        MorphChildren(live, fresh.get());
        stats.touched += int(new_nodes.size());
        return;
    }
    for (size_t i = 0; i < new_nodes.size(); ++i)
    {
        const RmlNode &node = new_nodes[i];
        if (i < old_nodes.size())
        {
            const RmlNode &old = old_nodes[i];
            if (Span(before, old) == Span(after, node))
                continue; // rendered the same: untouched
            Rml::Element *element = current[i];
            const bool same = old.text == node.text && (node.text ? rmlui_dynamic_cast<Rml::ElementText *>(element) != nullptr
                                                                   : element->GetTagName() == node.tag &&
                                                                         std::any_of(old.attributes.begin(), old.attributes.end(),
                                                                                     [](const auto &a) { return a.first == "slot"; }) ==
                                                                             std::any_of(node.attributes.begin(), node.attributes.end(),
                                                                                         [](const auto &a) { return a.first == "slot"; }));
            const auto id_of = [](const RmlNode &n) {
                for (const auto &a : n.attributes)
                    if (a.first == "id")
                        return Rml::StringUtilities::DecodeRml(a.second);
                return Rml::String();
            };
            // A component that takes another id is another component (MorphChildren).
            if (same && (node.text || !dynamic_cast<ComponentElement *>(element) || element->GetId() == id_of(node)))
            {
                PatchElement(element, before, old, after, node, stats);
                continue;
            }
            Rml::ElementPtr fresh = Instance(live, Span(after, node));
            if (fresh->GetNumChildren() == 1)
                live->ReplaceChild(fresh->RemoveChild(fresh->GetFirstChild()), element);
            ++stats.touched;
            continue;
        }
        Rml::ElementPtr fresh = Instance(live, Span(after, node));
        while (fresh->GetNumChildren() > 0)
            live->AppendChild(fresh->RemoveChild(fresh->GetFirstChild()));
        ++stats.touched;
    }
    for (size_t i = new_nodes.size(); i < current.size(); ++i)
    {
        live->RemoveChild(current[i]);
        ++stats.touched;
    }
}

// --- props a template only passes on ------------------------------------------------------
// A screen's template mostly hands its props to nested components (stats="{{ json(stats) }}").
// When every prop that changed is used only like that, the component does not render its
// template again: the attribute is set on the live element, which rebuilds that nested
// component alone. A drag of the store's amount changed the header figures, the deal row and
// the button; s-trade, c-screen and c-trade-list each rendered and diffed their whole template
// for it.
struct ForwardSite
{
    std::string tag, id;      // the element: its tag and literal id (empty: the template's only such tag)
    std::string attribute;    // where the value goes
    std::string path;         // what goes there: the changed path, or an object around it passed whole
    bool json = false;        // {{ json(path) }}, else {{ path }}
};

bool IsNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// Where `path` (a prop, or one key of an object prop: "list.rows") is used in `body`, if every
// use is a whole attribute value {{ json(path) }} or {{ path }} of an element outside loops, or a
// t-if test of an enclosing path whose truth did not change (`truth_kept`). Empty optional: some
// other use; the template must be rendered.
std::optional<std::vector<ForwardSite>> ForwardSites(std::string_view body, const std::string &path, const std::function<bool(const std::string &)> &truth_kept)
{
    std::vector<ForwardSite> sites;
    const std::string root = path.substr(0, path.find('.'));
    for (size_t at = body.find(root); at != std::string_view::npos; at = body.find(root, at + 1))
    {
        if ((at > 0 && (IsNameChar(body[at - 1]) || body[at - 1] == '.' || body[at - 1] == '-')) ||
            (at + root.size() < body.size() && (IsNameChar(body[at + root.size()]) || body[at + root.size()] == '-')))
            continue; // part of another name (or of a class name)
        // Only expressions read props: {{ … }} and the tests and lists of t-if / t-for; an
        // attribute name or literal text of the same spelling is no use.
        const size_t hole = body.rfind("{{", at), hole_end = body.rfind("}}", at);
        const bool in_hole = hole != std::string_view::npos && (hole_end == std::string_view::npos || hole_end < hole);
        if (!in_hole)
        {
            const size_t tag_open = body.rfind('<', at);
            const std::string_view head = tag_open == std::string_view::npos ? std::string_view() : body.substr(tag_open, at - tag_open);
            const bool control = head.rfind("<t-if", 0) == 0 || head.rfind("<t-for", 0) == 0;
            if (!control || head.find('>') != std::string_view::npos || head.find('"') == std::string_view::npos)
                continue;
        }
        size_t end = at + root.size();
        while (end + 1 < body.size() && body[end] == '.' && IsNameChar(body[end + 1]))
        {
            ++end;
            while (end < body.size() && IsNameChar(body[end]))
                ++end;
        }
        const std::string used(body.substr(at, end - at));
        const bool related = used == path || used.rfind(path + ".", 0) == 0 || path.rfind(used + ".", 0) == 0;
        if (!related)
            continue;
        // The enclosing tag and the attribute this use is in (a '<' or '>' in an expression is
        // inside quotes).
        size_t open = body.rfind('<', at);
        while (open != std::string_view::npos && open > 0 && !std::isalpha(static_cast<unsigned char>(body[open + 1])))
            open = body.rfind('<', open - 1);
        size_t close = std::string_view::npos;
        bool quoted = false;
        for (size_t i = open == std::string_view::npos ? body.size() : open; i < body.size(); ++i)
        {
            if (body[i] == '"')
                quoted = !quoted;
            else if (body[i] == '>' && !quoted)
            {
                close = i;
                break;
            }
        }
        if (open == std::string_view::npos || close == std::string_view::npos || close < at)
            return std::nullopt; // in text, not in a tag
        const std::string_view tag_text = body.substr(open, close - open);
        size_t name_end = 1;
        while (name_end < tag_text.size() && (IsNameChar(tag_text[name_end]) || tag_text[name_end] == '-'))
            ++name_end;
        const std::string tag(tag_text.substr(1, name_end - 1));
        const size_t quote = body.rfind('"', at);
        const size_t equals = quote == std::string_view::npos ? std::string_view::npos : body.rfind('=', quote);
        if (equals == std::string_view::npos || equals < open)
            return std::nullopt;
        size_t name_begin = equals;
        while (name_begin > open && (IsNameChar(body[name_begin - 1]) || body[name_begin - 1] == '-'))
            --name_begin;
        const std::string attribute(body.substr(name_begin, equals - name_begin));
        const size_t value_end = body.find('"', at);
        std::string value(body.substr(quote + 1, value_end - quote - 1));
        value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == ' '; }), value.end());
        if (tag == "t-if" && attribute == "test" && value == used && used != path && truth_kept(used))
            continue; // only the truth of an enclosing path, which the change kept
        if (tag.rfind("t-", 0) == 0)
            return std::nullopt;
        // The changed path itself, or the object around it passed on whole ({{ json(deal) }} for deal.value).
        const bool json = value == "{{json(" + used + ")}}";
        if (!(used == path && (json || value == "{{" + path + "}}")) && !(json && path.rfind(used + ".", 0) == 0))
            return std::nullopt;
        // Not in a loop: one element per use.
        const std::string_view before = body.substr(0, open);
        size_t loops = 0, at_loop = 0;
        while ((at_loop = before.find("<t-for", at_loop)) != std::string_view::npos)
            ++loops, ++at_loop;
        size_t ends = 0, at_end = 0;
        while ((at_end = before.find("</t-for>", at_end)) != std::string_view::npos)
            ++ends, ++at_end;
        if (loops != ends)
            return std::nullopt;
        ForwardSite site;
        site.tag = tag;
        site.attribute = attribute;
        site.path = used;
        site.json = json;
        const size_t id_at = tag_text.find(" id=\"");
        if (id_at != std::string_view::npos)
        {
            const size_t id_end = tag_text.find('"', id_at + 5);
            site.id = std::string(tag_text.substr(id_at + 5, id_end - id_at - 5));
            if (site.id.find("{{") != std::string::npos)
                return std::nullopt;
        }
        sites.push_back(std::move(site));
    }
    return sites;
}

// The live element of a site under `self`: by id, or the only element of the tag.
Rml::Element *SiteElement(Rml::Element *self, const ForwardSite &site)
{
    Rml::ElementList found;
    self->QuerySelectorAll(found, site.id.empty() ? site.tag : site.tag + "#" + site.id);
    return found.size() == 1 ? found.front() : nullptr;
}

} // namespace

bool IsGenericAttribute(const std::string &attribute)
{
    return attribute == "id" || attribute == "class" || attribute == "style" || attribute == "props" || attribute == "slot" ||
           attribute == "rmlui-layout-boundary" || attribute.rfind("data-", 0) == 0;
}

std::string CheckPropValue(const PropSpec &prop, const Json &value)
{
    if (prop.type != PropSpec::Type::Enum || value.is_null() || prop.options.empty())
        return {};
    for (const Json &option : prop.options)
        if (option == value || ToText(option) == ToText(value))
            return {};
    return "'" + ToText(value) + "' is not a value of " + prop.name;
}

int ReportUnknownComponents(Rml::Element *root)
{
    int found = 0;
    for (int i = 0; root && i < root->GetNumChildren(); ++i)
    {
        Rml::Element *child = root->GetChild(i);
        if (dynamic_cast<ComponentElement *>(child))
            continue;
        if (child->GetTagName().rfind("c-", 0) == 0)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Unknown component <%s> (%s): no spec in components.json", child->GetTagName().c_str(),
                              child->GetAddress().c_str());
            child->SetClass("ui-error", true);
            child->SetClass("ui-unknown", true); // an unknown tag is inline and empty: give it a box
            ++found;
        }
        found += ReportUnknownComponents(child);
    }
    return found;
}

void RegisterBehavior(const std::string &name, BehaviorFactory factory)
{
    Factories()[name] = factory;
}

const PropSpec *ComponentSpec::FindAttribute(const std::string &attribute) const
{
    for (const PropSpec &prop : props)
        if (prop.attribute == attribute)
            return &prop;
    return nullptr;
}

ComponentElement::ComponentElement(const Rml::String &tag, const ComponentSpec &spec, Services &services)
    : Rml::Element(tag), spec_(spec), services_(services)
{
    for (const std::string &name : spec.behaviors)
    {
        const auto factory = Factories().find(name);
        if (factory == Factories().end())
            Rml::Log::Message(Rml::Log::LT_ERROR, "Component %s: unknown behaviour '%s'", tag.c_str(), name.c_str());
        else
            behaviors_.push_back(factory->second());
    }
}

ComponentElement::~ComponentElement()
{
    services_.Forget(*this);
}

Json ComponentElement::Prop(const std::string &name) const
{
    const auto found = props_.find(name);
    if (found != props_.end() && !found->is_null())
        return *found;
    for (const PropSpec &prop : spec_.props)
        if (prop.name == name)
            return prop.fallback;
    return Json();
}

void ComponentElement::SetProps(const Json &props)
{
    if (!props.is_object())
    {
        ReportError("props must be a JSON object");
        return;
    }
    for (const auto &item : props.items())
    {
        const bool known = std::any_of(spec_.props.begin(), spec_.props.end(), [&](const PropSpec &prop) { return prop.name == item.key(); });
        if (!known)
            ReportError("unknown prop '" + item.key() + "'");
        props_[item.key()] = item.value();
    }
    dirty_ = true;
}

void ComponentElement::ReportError(const std::string &what)
{
    Rml::Log::Message(Rml::Log::LT_ERROR, "Component <%s> (%s): %s", GetTagName().c_str(), GetAddress().c_str(), what.c_str());
    error_ = true;
    ApplyClasses();
}

void ComponentElement::SetPropQuiet(const std::string &name, Json value)
{
    props_[name] = std::move(value);
}

void ComponentElement::SetLive(const std::string &name, bool on)
{
    const auto found = std::find(live_classes_.begin(), live_classes_.end(), name);
    if (on == (found != live_classes_.end()))
        return;
    if (on)
        live_classes_.push_back(name);
    else
        live_classes_.erase(found);
    ApplyClasses();
}

void ComponentElement::Emit(const std::string &verb, const std::string &value)
{
    services_.Emit(*this, verb, value);
}

void ComponentElement::OnAttributeChange(const Rml::ElementAttributes &changed)
{
    Rml::Element::OnAttributeChange(changed);
    for (const auto &entry : changed)
    {
        if (entry.first == "props") // a whole props object, like dc-props on the canvas
        {
            const Json parsed = Json::parse(entry.second.Get<Rml::String>(), nullptr, false);
            if (parsed.is_object())
                SetProps(parsed);
            else
                ReportError("props is not a JSON object");
            continue;
        }
        if (entry.first == "data-behavior") // one more behaviour for this element (HUD layout: hold, swipe)
        {
            const std::string name = entry.second.Get<Rml::String>();
            const auto factory = Factories().find(name);
            if (factory == Factories().end())
                ReportError("unknown behaviour '" + name + "'");
            else if (std::find(spec_.behaviors.begin(), spec_.behaviors.end(), name) == spec_.behaviors.end() && extra_behavior_ != name)
            {
                extra_behavior_ = name;
                behaviors_.push_back(factory->second());
            }
            continue;
        }
        if (const PropSpec *prop = spec_.FindAttribute(entry.first))
        {
            // A removed attribute (a morph from another instance of the component) returns the
            // prop to its default; it is not an empty, "present" value (a bool would read true).
            if (HasAttribute(entry.first))
                props_[prop->name] = ParseAttribute(*prop, entry.second);
            else
                props_.erase(prop->name);
            dirty_ = true;
        }
        else if (!IsGenericAttribute(entry.first))
            ReportError("unknown attribute '" + entry.first + "'");
    }
}

void ComponentElement::OnUpdate()
{
    Rml::Element::OnUpdate();
    if (dirty_)
        RebuildNow();
}

void ComponentElement::OnLayout()
{
    Rml::Element::OnLayout();
    ++services_.Counters().laid_out;
}

void ComponentElement::RebuildNow()
{
    // FrameStats::rebuild_ms: the outermost rebuild only (a morph rebuilds nested components inside it).
    static int depth = 0;
    const auto started = std::chrono::steady_clock::now();
    ++depth;
    struct Timed
    {
        FrameStats &stats;
        std::chrono::steady_clock::time_point started;
        ~Timed()
        {
            if (--depth == 0)
                stats.rebuild_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
    } timed{services_.Counters(), started};
    dirty_ = false;
    if (built_ && GetOwnerDocument() && Forward())
    {
        for (const auto &behavior : behaviors_)
            behavior->Rebuilt(*this);
        return;
    }
    // Counted for every re-render, also one whose output is unchanged: a behaviour may still
    // change what is shown in Rebuilt (Prepare draws a frame for it).
    ++services_.Counters().rebuilds;

    // First build: the element's own children are the content for the slot.
    std::vector<Rml::ElementPtr> kept;
    bool has_children = false;
    if (!built_)
    {
        while (GetNumChildren() > 0)
            kept.push_back(RemoveChild(GetFirstChild()));
        has_children = !kept.empty();
    }
    else if (pending_slot_)
    {
        kept = std::move(slot_content_);
        slot_content_.clear();
        pending_slot_ = false;
        has_children = !kept.empty();
    }
    else if (Rml::Element *slot = FindSlot(this))
        has_children = slot->GetNumChildren() > 0;

    Json scope = Json::object();
    prop_classes_.clear();
    for (const PropSpec &prop : spec_.props)
    {
        Json value = Prop(prop.name);
        const std::string wrong = CheckPropValue(prop, value);
        if (!wrong.empty())
            ReportError(wrong);
        if (prop.as_class)
        {
            if (prop.type == PropSpec::Type::Bool)
            {
                if (Truthy(value))
                    prop_classes_.push_back(prop.attribute);
            }
            else if (!ToText(value).empty())
                prop_classes_.push_back(prop.attribute + "-" + ToText(value));
        }
        scope[prop.name] = std::move(value);
    }
    scope["spec"] = spec_.constants;
    scope["children"] = has_children;
    scope["dp"] = services_.Density();
    scope["safe"] = services_.SafeArea();
    for (const auto &rule : spec_.class_rules)
    {
        try
        {
            if (Truthy(EvaluateExpression(rule.second, scope)))
                prop_classes_.push_back(rule.first);
        }
        catch (const std::exception &error)
        {
            ReportError("class " + rule.first + ": " + error.what());
        }
    }

    std::string rml;
    try
    {
        rml = RenderTemplate(spec_.body, scope, &memo_);
    }
    catch (const std::exception &error)
    {
        ReportError(std::string("template: ") + error.what());
    }
    Rml::ElementDocument *document = GetOwnerDocument();
    if (!built_ || !document)
    {
        // constants.layoutBoundary: the component's size never depends on its contents (a header bar of fixed
        // height across the screen), so a change inside it lays out only it (patches/rmlui-layout-boundary.patch).
        if (!built_ && Truthy(spec_.constants.value("layoutBoundary", Json(false))))
            SetAttribute("rmlui-layout-boundary", "");
        SetInnerRML(rml);
        ++services_.Counters().touched;
        size_t at = 0;
        built_tree_.clear();
        built_tree_ok_ = ParseRml(rml, at, built_tree_, {});
    }
    else if (rml != built_rml_)
    {
        // Later builds patch the existing tree in place: only what the render changed.
        std::vector<RmlNode> fresh_tree;
        size_t at = 0;
        if (built_tree_ok_ && ParseRml(rml, at, fresh_tree, {}))
        {
            PatchStats stats;
            PatchChildren(this, built_rml_, built_tree_, rml, fresh_tree, stats);
            services_.Counters().touched += stats.touched;
            services_.Counters().patch_fallbacks += stats.fallbacks;
            built_tree_ = std::move(fresh_tree);
        }
        else
        {
            Rml::ElementPtr fresh = document->CreateElement("div");
            fresh->SetInnerRML(rml);
            MorphChildren(this, fresh.get());
            ++services_.Counters().patch_fallbacks;
            ++services_.Counters().touched;
            at = 0;
            fresh_tree.clear();
            built_tree_ok_ = ParseRml(rml, at, fresh_tree, {});
            built_tree_ = std::move(fresh_tree);
        }
    }
    // Else the template gave what is shown already (a model revision that did not touch this
    // component): no tree is parsed, compared or laid out.
    built_rml_ = std::move(rml);
    rendered_values_ = Json::object();
    for (const PropSpec &prop : spec_.props)
        rendered_values_[prop.name] = Prop(prop.name);
    if (!kept.empty())
    {
        if (Rml::Element *slot = FindSlot(this))
            for (Rml::ElementPtr &child : kept)
                slot->AppendChild(std::move(child));
        else
            ReportError("content passed to a component whose template has no slot");
    }
    built_ = true;
    built_children_ = has_children;
    ReportUnknownComponents(this);
    ApplyBoxSize(scope);
    ApplyClasses();
    for (const auto &behavior : behaviors_)
        behavior->Rebuilt(*this);
}

// constants.boxSize: the name of a prop that is the component's width and height in dp (C-Icon:
// its size). A box whose size is set needs no formatting of its content to be measured: an
// icon in a header's nested flex rows was formatted about 500 times per layout of the header.
void ComponentElement::ApplyBoxSize(const Json &scope)
{
    const auto prop = spec_.constants.find("boxSize");
    if (prop == spec_.constants.end() || !prop->is_string())
        return;
    const auto value = scope.find(prop->get<std::string>());
    const float size = value != scope.end() && value->is_number() ? value->get<float>() : -1.0f;
    if (size == box_size_)
        return;
    box_size_ = size;
    if (size > 0.0f)
    {
        SetProperty(Rml::PropertyId::Width, Rml::Property(size, Rml::Unit::DP));
        SetProperty(Rml::PropertyId::Height, Rml::Property(size, Rml::Unit::DP));
    }
    else
    {
        RemoveProperty(Rml::PropertyId::Width);
        RemoveProperty(Rml::PropertyId::Height);
        ReportError("constants.boxSize names prop '" + prop->get<std::string>() + "', which is not a number");
    }
}

// The props that changed since the last render, by path (a changed object prop by its keys).
// When the template only passes each of them on (ForwardSites), they are set on the elements
// that take them and the template is not rendered. False: it must be.
bool ComponentElement::Forward()
{
    if (pending_slot_ || error_)
        return false;
    std::vector<std::pair<std::string, Json>> changed; // path -> new value
    for (const PropSpec &prop : spec_.props)
    {
        const Json now = Prop(prop.name);
        const auto before = rendered_values_.find(prop.name);
        if (before == rendered_values_.end())
            return false;
        if (*before == now)
            continue;
        // Classes and class rules read props as they are.
        if (prop.as_class || std::any_of(spec_.class_rules.begin(), spec_.class_rules.end(),
                                         [&](const auto &rule) { return rule.second.find(prop.name) != std::string::npos; }))
            return false;
        if (before->is_object() && now.is_object())
        {
            for (const auto &item : now.items())
                if (!before->contains(item.key()) || (*before)[item.key()] != item.value())
                    changed.emplace_back(prop.name + "." + item.key(), item.value());
            for (const auto &item : before->items())
                if (!now.contains(item.key()))
                    changed.emplace_back(prop.name + "." + item.key(), Json());
        }
        else
            changed.emplace_back(prop.name, now);
    }
    if (changed.empty())
        return false; // nothing for this: the ordinary path, which finds the render unchanged
    struct Set
    {
        Rml::Element *element;
        std::string attribute, value;
    };
    std::vector<Set> sets;
    const auto value_at = [this](const Json &values, const std::string &path) {
        const Json *value = &values;
        size_t at = 0;
        while (value && at <= path.size())
        {
            const size_t dot = std::min(path.find('.', at), path.size());
            const std::string key = path.substr(at, dot - at);
            value = value->is_object() && value->contains(key) ? &(*value)[key] : nullptr;
            at = dot + 1;
        }
        return value ? *value : Json();
    };
    Json current = Json::object();
    for (const PropSpec &prop : spec_.props)
        current[prop.name] = Prop(prop.name);
    for (const auto &[path, value] : changed)
    {
        const auto truth_kept = [&](const std::string &enclosing) {
            return Truthy(value_at(rendered_values_, enclosing)) == Truthy(value_at(current, enclosing));
        };
        // The analysis of a path is the template's: kept per spec, unless it depended on a truth.
        static std::map<std::pair<const ComponentSpec *, std::string>, std::optional<std::vector<ForwardSite>>> analysed;
        bool asked = false;
        const auto truth = [&](const std::string &enclosing) {
            asked = true;
            return truth_kept(enclosing);
        };
        const auto key = std::make_pair(&spec_, path);
        auto cached = analysed.find(key);
        std::optional<std::vector<ForwardSite>> found;
        if (cached != analysed.end())
            found = cached->second;
        else
        {
            found = ForwardSites(spec_.body, path, truth);
            if (!asked)
                analysed.emplace(key, found);
        }
        const auto &sites = found;
        if (!sites || sites->empty())
            return false;
        for (const ForwardSite &site : *sites)
        {
            Rml::Element *element = SiteElement(this, site);
            if (!element)
                return false;
            const Json passed = site.path == path ? value : value_at(current, site.path);
            sets.push_back({element, site.attribute, site.json ? passed.dump() : ToText(passed)});
        }
    }
    for (const Set &set : sets)
        if (set.element->GetAttribute<Rml::String>(set.attribute, "") != set.value)
        {
            set.element->SetAttribute(set.attribute, set.value);
            ++services_.Counters().touched;
        }
    ++services_.Counters().forwarded;
    rendered_values_ = std::move(current);
    // The live tree is no longer the last render: the next render morphs it whole once.
    built_tree_ok_ = false;
    built_rml_.clear();
    return true;
}

void ComponentElement::SyncSlot(Rml::Element *content)
{
    const bool wanted = content->GetNumChildren() > 0;
    if (!built_)
    {
        // Not built yet: the element's own children are still the slot content of the first build.
        while (GetNumChildren() > 0)
            RemoveChild(GetFirstChild());
        while (content->GetNumChildren() > 0)
            AppendChild(content->RemoveChild(content->GetFirstChild()));
        return;
    }
    Rml::Element *slot = FindSlot(this);
    if (wanted == built_children_ && (slot || !wanted))
    {
        if (slot)
            MorphChildren(slot, content);
        return;
    }
    // Content appeared or disappeared: the template decides on `children` whether it has a
    // slot at all, so rebuild with the new content, as the first build does.
    if (slot)
        while (slot->GetNumChildren() > 0)
            slot->RemoveChild(slot->GetFirstChild());
    slot_content_.clear();
    while (content->GetNumChildren() > 0)
        slot_content_.push_back(content->RemoveChild(content->GetFirstChild()));
    pending_slot_ = true;
    RebuildNow();
}

void ComponentElement::ApplyClasses()
{
    std::vector<std::string> wanted = prop_classes_;
    if (error_)
        wanted.push_back("ui-error");
    for (const std::string &name : live_classes_)
        if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            wanted.push_back(name);
    if (wanted == applied_classes_)
        return;
    for (const std::string &name : applied_classes_)
        if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            SetClass(name, false);
    for (const std::string &name : wanted)
        SetClass(name, true);
    applied_classes_ = std::move(wanted);
}

bool ComponentElement::PointerExclusive(const Pointer &pointer) const
{
    for (const auto &behavior : behaviors_)
        if (behavior->Exclusive(pointer))
            return true;
    return false;
}

bool ComponentElement::PointerDown(const Pointer &pointer)
{
    bool taken = false;
    for (const auto &behavior : behaviors_)
        taken = behavior->Down(*this, pointer) || taken;
    return taken;
}

void ComponentElement::PointerMove(const Pointer &pointer)
{
    for (const auto &behavior : behaviors_)
        behavior->Move(*this, pointer);
}

void ComponentElement::PointerUp(const Pointer &pointer, bool cancelled)
{
    for (const auto &behavior : behaviors_)
        behavior->Up(*this, pointer, cancelled);
}

void ComponentElement::TouchRegions(std::vector<Rml::Element *> &out)
{
    for (const auto &behavior : behaviors_)
        behavior->Regions(*this, out);
}

void ComponentElement::Tick(double now)
{
    for (const auto &behavior : behaviors_)
        behavior->Tick(*this, now);
}

class ComponentRegistry::Instancer final : public Rml::ElementInstancer
{
  public:
    Instancer(const ComponentSpec &spec, Services &services) : spec_(spec), services_(services)
    {
    }
    Rml::ElementPtr InstanceElement(Rml::Element *, const Rml::String &tag, const Rml::XMLAttributes &) override
    {
        return Rml::ElementPtr(new ComponentElement(tag, spec_, services_));
    }
    void ReleaseElement(Rml::Element *element) override
    {
        delete element;
    }

  private:
    const ComponentSpec &spec_;
    Services &services_;
};

std::unique_ptr<ComponentSpec> ParseComponentSpec(const Json &entry)
{
    static const std::unordered_map<std::string, PropSpec::Type> types = {
        {"text", PropSpec::Type::Text}, {"bool", PropSpec::Type::Bool},     {"int", PropSpec::Type::Int},
        {"number", PropSpec::Type::Number}, {"enum", PropSpec::Type::Enum}, {"json", PropSpec::Type::Json}};
    auto spec = std::make_unique<ComponentSpec>();
    spec->tag = entry.at("tag").get<std::string>();
    spec->body = entry.value("template", std::string());
    spec->constants = entry.value("constants", Json::object());
    // Named locals: a range-for over a temporary's items() would outlive the temporary.
    const Json behaviors = entry.value("behaviors", Json::array());
    const Json classes = entry.value("classes", Json::object());
    const Json props = entry.value("props", Json::object());
    for (const Json &name : behaviors)
        spec->behaviors.push_back(name.get<std::string>());
    for (const auto &rule : classes.items())
        spec->class_rules.emplace_back(rule.key(), rule.value().get<std::string>());
    for (const auto &item : props.items())
    {
        PropSpec prop;
        prop.name = item.key();
        prop.attribute = Kebab(prop.name);
        const std::string type_name = item.value().value("type", std::string("text"));
        const auto type = types.find(type_name);
        if (type == types.end())
            throw std::runtime_error(spec->tag + ": prop " + prop.name + " has unknown type " + type_name);
        prop.type = type->second;
        prop.fallback = item.value().value("default", Json());
        prop.as_class =
            item.value().value("class", prop.type == PropSpec::Type::Bool || prop.type == PropSpec::Type::Enum);
        const Json options = item.value().value("options", Json::array());
        for (const Json &option : options)
            prop.options.push_back(option);
        spec->props.push_back(std::move(prop));
    }
    return spec;
}

void ComponentRegistry::Load(const Json &pack)
{
    for (const Json &entry : pack.at("components"))
    {
        std::unique_ptr<ComponentSpec> spec = ParseComponentSpec(entry);
        auto instancer = std::make_unique<Instancer>(*spec, services_);
        Rml::Factory::RegisterElementInstancer(spec->tag, instancer.get());
        instancers_.push_back(std::move(instancer));
        specs_[spec->tag] = std::move(spec);
    }
}

const ComponentSpec *ComponentRegistry::Find(const std::string &tag) const
{
    const auto found = specs_.find(tag);
    return found == specs_.end() ? nullptr : found->second.get();
}

ComponentElement *InteractiveComponentAt(Rml::Element *element, const Pointer &pointer)
{
    ComponentElement *closest = nullptr;
    for (; element; element = element->GetParentNode())
        if (auto *component = dynamic_cast<ComponentElement *>(element))
            if (component->HasBehaviors())
            {
                if (component->PointerExclusive(pointer))
                    return component;
                if (!closest)
                    closest = component;
            }
    return closest;
}

} // namespace storm::ui
