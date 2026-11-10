// Spec-driven components: every <c-…> tag is described by one entry of the UI pack's
// components.json (props, template, behaviours). The same spec feeds the design canvas.
#pragma once

#include "template.h"

#include <RmlUi/Core.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui
{

class ComponentElement;

struct PropSpec
{
    enum class Type
    {
        Text,
        Bool,
        Int,
        Number,
        Enum,
        Json
    };
    std::string name;      // camelCase, as in the canvas component
    std::string attribute; // kebab-case RML attribute
    Type type = Type::Text;
    Json fallback;
    bool as_class = false; // bool → "name", enum → "name-value" on the root element
    std::vector<Json> options; // enum values
};

struct ComponentSpec
{
    std::string tag;
    std::vector<PropSpec> props;
    std::string body; // template
    std::vector<std::string> behaviors;
    Json constants; // sizes and other numbers shared with the canvas ("spec" in templates)
    // Root classes computed from props: class name -> template expression.
    std::vector<std::pair<std::string, std::string>> class_rules;
    const PropSpec *FindAttribute(const std::string &attribute) const;
};

// One entry of components.json.
std::unique_ptr<ComponentSpec> ParseComponentSpec(const Json &entry);
// Attributes every component accepts besides its props: id, class, style, props, slot, data-*.
bool IsGenericAttribute(const std::string &attribute);
// Empty when `value` is acceptable for the prop, else what is wrong (enum value not listed).
std::string CheckPropValue(const PropSpec &prop, const Json &value);
// Logs and marks (class "ui-error") every <c-…> element under `root` that has no spec;
// returns how many it found. Nested components check their own content.
int ReportUnknownComponents(Rml::Element *root);

// Services the host gives to components and behaviours.
class Services
{
  public:
    virtual ~Services() = default;
    virtual double Now() const = 0;
    virtual float Density() const = 0;
    // Safe-area insets in dp: {left, top, right, bottom}.
    virtual Json SafeArea() const = 0;
    // A number from pack.json "settings" (timings from the design tokens).
    virtual double Setting(const std::string &name, double fallback) const = 0;
    virtual void Emit(ComponentElement &source, const std::string &verb, const std::string &value) = 0;
    virtual void Toast(const std::string &text, const std::string &tone) = 0;
    // Shows the long-press tooltip next to `anchor`; an empty text hides it.
    virtual void Tooltip(Rml::Element *anchor, const std::string &text, bool below) = 0;
    // Keeps Tick() coming to this component until it asks to stop.
    virtual void RequestTicks(ComponentElement &component, bool enable) = 0;
    virtual void Forget(ComponentElement &component) = 0;
};

struct Pointer
{
    int id = 0;
    Rml::Vector2f position;   // context coordinates, physical pixels
    Rml::Vector2f start;      // where it went down
    Rml::Element *target = nullptr; // element under the finger at down
};

class Behavior
{
  public:
    virtual ~Behavior() = default;
    // True when the behaviour keeps the pointer for itself (drags): such a pointer never
    // scrolls a list and never reaches other elements.
    virtual bool Exclusive(const Pointer &) const
    {
        return false;
    }
    virtual bool Down(ComponentElement &, const Pointer &)
    {
        return false;
    }
    virtual void Move(ComponentElement &, const Pointer &)
    {
    }
    // `cancelled`: the finger left for a scroll or the system took the gesture.
    virtual void Up(ComponentElement &, const Pointer &, bool /*cancelled*/)
    {
    }
    virtual void Tick(ComponentElement &, double /*now*/)
    {
    }
    virtual void Rebuilt(ComponentElement &)
    {
    }
};

using BehaviorFactory = std::unique_ptr<Behavior> (*)();
void RegisterBehavior(const std::string &name, BehaviorFactory factory);
void RegisterBuiltinBehaviors();

class ComponentElement final : public Rml::Element
{
  public:
    ComponentElement(const Rml::String &tag, const ComponentSpec &spec, Services &services);
    ~ComponentElement() override;

    const Json &Props() const
    {
        return props_;
    }
    Json Prop(const std::string &name) const;
    bool Flag(const std::string &name) const
    {
        return Truthy(Prop(name));
    }
    // Merges the given props and rebuilds on the next update.
    void SetProps(const Json &props);
    // Changes one prop without rebuilding the inner tree (drag feedback); the behaviour
    // updates the affected elements itself.
    void SetPropQuiet(const std::string &name, Json value);
    // Live state classes ("pressed", "active", "done") owned by behaviours.
    void SetLive(const std::string &name, bool on);
    void Emit(const std::string &verb, const std::string &value = {});
    Services &Host()
    {
        return services_;
    }
    const ComponentSpec &Spec() const
    {
        return spec_;
    }
    bool HasBehaviors() const
    {
        return !behaviors_.empty();
    }

    // Pointer routing (called by the host).
    bool PointerExclusive(const Pointer &pointer) const;
    bool PointerDown(const Pointer &pointer);
    void PointerMove(const Pointer &pointer);
    void PointerUp(const Pointer &pointer, bool cancelled);
    void Tick(double now);
    void RebuildNow();

  protected:
    void OnAttributeChange(const Rml::ElementAttributes &changed) override;
    void OnUpdate() override;

  private:
    void ApplyClasses();
    // A wrong prop, enum value or template: logged with the element and drawn as the
    // striped "ui-error" marker instead of a silent default.
    void ReportError(const std::string &what);

    const ComponentSpec &spec_;
    Services &services_;
    Json props_ = Json::object();
    std::vector<std::unique_ptr<Behavior>> behaviors_;
    std::vector<std::string> prop_classes_;
    std::vector<std::string> live_classes_;
    std::vector<std::string> applied_classes_;
    bool dirty_ = true;
    bool built_ = false;
    bool error_ = false; // ReportError happened since the last rebuild
};

class ComponentRegistry
{
  public:
    explicit ComponentRegistry(Services &services) : services_(services)
    {
    }
    // Parses components.json and registers one element instancer per component tag.
    void Load(const Json &pack);
    const ComponentSpec *Find(const std::string &tag) const;

  private:
    class Instancer;
    Services &services_;
    std::unordered_map<std::string, std::unique_ptr<ComponentSpec>> specs_;
    std::vector<std::unique_ptr<Rml::ElementInstancer>> instancers_;
};

// Closest ancestor-or-self that is a component with behaviours.
ComponentElement *InteractiveComponentAt(Rml::Element *element);

} // namespace storm::ui
