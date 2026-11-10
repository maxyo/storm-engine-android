#include "components.h"

#include <algorithm>
#include <cstdlib>

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

} // namespace

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
        return;
    for (const auto &item : props.items())
        props_[item.key()] = item.value();
    dirty_ = true;
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
            continue;
        }
        if (const PropSpec *prop = spec_.FindAttribute(entry.first))
        {
            props_[prop->name] = ParseAttribute(*prop, entry.second);
            dirty_ = true;
        }
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

    std::vector<Rml::ElementPtr> kept;
    if (Rml::Element *slot = built_ ? FindSlot(this) : this)
        while (slot->GetNumChildren() > 0)
            kept.push_back(slot->RemoveChild(slot->GetFirstChild()));

    Json scope = Json::object();
    prop_classes_.clear();
    for (const PropSpec &prop : spec_.props)
    {
        Json value = Prop(prop.name);
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
    scope["children"] = !kept.empty();
    scope["dp"] = services_.Density();
    for (const auto &rule : spec_.class_rules)
    {
        try
        {
            if (Truthy(EvaluateExpression(rule.second, scope)))
                prop_classes_.push_back(rule.first);
        }
        catch (const std::exception &error)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Component %s, class %s: %s", GetTagName().c_str(), rule.first.c_str(), error.what());
        }
    }

    try
    {
        SetInnerRML(RenderTemplate(spec_.body, scope));
    }
    catch (const std::exception &error)
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Component %s: %s", GetTagName().c_str(), error.what());
        SetInnerRML("");
    }
    built_ = true;

    if (!kept.empty())
    {
        if (Rml::Element *slot = FindSlot(this))
            for (Rml::ElementPtr &child : kept)
                slot->AppendChild(std::move(child));
    }
    ApplyClasses();
    for (const auto &behavior : behaviors_)
        behavior->Rebuilt(*this);
}

void ComponentElement::ApplyClasses()
{
    std::vector<std::string> wanted = prop_classes_;
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

void ComponentRegistry::Load(const Json &pack)
{
    static const std::unordered_map<std::string, PropSpec::Type> types = {
        {"text", PropSpec::Type::Text}, {"bool", PropSpec::Type::Bool},     {"int", PropSpec::Type::Int},
        {"number", PropSpec::Type::Number}, {"enum", PropSpec::Type::Enum}, {"json", PropSpec::Type::Json}};
    for (const Json &entry : pack.at("components"))
    {
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
            const auto type = types.find(item.value().value("type", std::string("text")));
            prop.type = type == types.end() ? PropSpec::Type::Text : type->second;
            prop.fallback = item.value().value("default", Json());
            prop.as_class =
                item.value().value("class", prop.type == PropSpec::Type::Bool || prop.type == PropSpec::Type::Enum);
            spec->props.push_back(std::move(prop));
        }
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
