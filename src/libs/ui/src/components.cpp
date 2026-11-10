#include "components.h"

#include <RmlUi/Core/ElementText.h>

#include <algorithm>
#include <cstdlib>
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
        if (!fresh->HasAttribute(entry.first))
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
    if (live->GetTagName().rfind("ui-", 0) == 0)
        return; // code-drawn elements own their children (ui-clamp keeps its cut text)
    if (auto *component = dynamic_cast<ComponentElement *>(live))
    {
        // A nested component rebuilds itself from its attributes; only content passed
        // into its slot comes from this template.
        if (Rml::Element *slot = FindSlot(component))
            MorphChildren(slot, fresh);
        return;
    }
    MorphChildren(live, fresh);
}

void MorphChildren(Rml::Element *live, Rml::Element *fresh)
{
    std::vector<Rml::ElementPtr> wanted;
    while (fresh->GetNumChildren() > 0)
        wanted.push_back(fresh->RemoveChild(fresh->GetFirstChild()));
    for (size_t i = 0; i < wanted.size(); ++i)
    {
        Rml::Element *current = static_cast<int>(i) < live->GetNumChildren() ? live->GetChild(static_cast<int>(i)) : nullptr;
        if (current && current->GetTagName() == wanted[i]->GetTagName())
            SyncElement(current, wanted[i].get());
        else if (current)
            live->ReplaceChild(std::move(wanted[i]), current);
        else
            live->AppendChild(std::move(wanted[i]));
    }
    while (live->GetNumChildren() > static_cast<int>(wanted.size()))
        live->RemoveChild(live->GetLastChild());
}

} // namespace

bool IsGenericAttribute(const std::string &attribute)
{
    return attribute == "id" || attribute == "class" || attribute == "style" || attribute == "props" || attribute == "slot" ||
           attribute.rfind("data-", 0) == 0;
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
        if (const PropSpec *prop = spec_.FindAttribute(entry.first))
        {
            props_[prop->name] = ParseAttribute(*prop, entry.second);
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

void ComponentElement::RebuildNow()
{
    dirty_ = false;

    // First build: the element's own children are the content for the slot.
    std::vector<Rml::ElementPtr> kept;
    bool has_children = false;
    if (!built_)
    {
        while (GetNumChildren() > 0)
            kept.push_back(RemoveChild(GetFirstChild()));
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
        rml = RenderTemplate(spec_.body, scope);
    }
    catch (const std::exception &error)
    {
        ReportError(std::string("template: ") + error.what());
    }
    Rml::ElementDocument *document = GetOwnerDocument();
    if (!built_ || !document)
    {
        SetInnerRML(rml);
        if (!kept.empty())
            if (Rml::Element *slot = FindSlot(this))
                for (Rml::ElementPtr &child : kept)
                    slot->AppendChild(std::move(child));
    }
    else
    {
        // Later builds patch the existing tree in place.
        Rml::ElementPtr fresh = document->CreateElement("div");
        fresh->SetInnerRML(rml);
        MorphChildren(this, fresh.get());
    }
    built_ = true;
    ReportUnknownComponents(this);
    ApplyClasses();
    for (const auto &behavior : behaviors_)
        behavior->Rebuilt(*this);
}

void ComponentElement::ApplyClasses()
{
    std::vector<std::string> wanted = prop_classes_;
    if (error_)
        wanted.push_back("ui-error");
    for (const std::string &name : live_classes_)
        if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            wanted.push_back(name);
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

ComponentElement *InteractiveComponentAt(Rml::Element *element)
{
    for (; element; element = element->GetParentNode())
        if (auto *component = dynamic_cast<ComponentElement *>(element))
            if (component->HasBehaviors())
                return component;
    return nullptr;
}

} // namespace storm::ui
