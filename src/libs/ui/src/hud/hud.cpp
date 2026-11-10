// The HUD core: which scene draws the current gameplay mode, when it is visible, and the
// way its events and timed work run. Scenes are in their own files (land.cpp).
#include "hud.h"

#include <algorithm>

namespace storm::ui::hud
{

std::unique_ptr<Scene> CreateLandScene(Hud &hud);
std::unique_ptr<Scene> CreateSeaScene(Hud &hud);
std::unique_ptr<Scene> CreateWorldMapScene(Hud &hud);

namespace
{

constexpr double kIdle = 3600.0;
constexpr double kLineSeconds = 2.6; // an event line stays this long (pack setting toastMs)
constexpr size_t kLines = 3;
// The spyglass overlay is acknowledged this often while it is drawn; the engine keeps its
// own info hidden for a second after the last one (android_spyglass.hpp).
constexpr double kSpyglassHeartbeat = 0.3;
// The layout scene under the spyglass when no ported scene owns the mode (the platform still
// draws the sea HUD): the overlay alone, no elements.
constexpr const char *kSpyglassLayout = "sea";

const char *const kLandModes[] = {"explore", "melee"};
const char *const kSeaModes[] = {"sailing", "naval"};
const char *const kWorldMapModes[] = {"worldmap"};

// Ported HUD scenes by gameplay mode. Porting a zone = adding its scene here; the
// platform host stops drawing that mode's HUD because HostOwnsHud turns false for it.
const SceneEntry kScenes[] = {
    {"land", kLandModes, CreateLandScene},
    {"sea", kSeaModes, CreateSeaScene, "sea"},
    {"world-map", kWorldMapModes, CreateWorldMapScene, "worldmap"},
};

} // namespace

std::span<const SceneEntry> Scenes()
{
    return kScenes;
}

bool Ported(std::string_view mode)
{
    for (const SceneEntry &entry : kScenes)
        for (const char *name : entry.modes)
            if (mode == name)
                return true;
    return false;
}

std::string Lower(std::string text)
{
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

void Input::On(const std::string &id, const std::string &verb, Handler handler)
{
    handlers_[id + "\n" + verb] = std::move(handler);
}

void Input::Ignore(const std::string &id, const std::string &verb)
{
    handlers_[id + "\n" + verb] = [](const Event &) {};
}

Hud::Hud(Mount &mount, std::span<const SceneEntry> scenes) : mount_(mount), scenes_(scenes)
{
}

Hud::~Hud() = default;

void Hud::Reject(const std::string &channel, const std::string &why)
{
    Error("state channel " + channel + ": " + why);
}

void Hud::Error(const std::string &line)
{
    if (reported_.insert(line).second)
        mount_.Error(line);
}

void Hud::Send(const std::string &verb, const std::string &value, const std::string &slot)
{
    Action action;
    action.screen = "hud";
    action.node = slot;
    action.verb = verb;
    action.value = value;
    mount_.Send(action);
}

Json Hud::LogLines() const
{
    Json lines = Json::array();
    for (size_t i = lines_.size(); i-- > 0;)
        lines.push_back(Json{{"text", lines_[i].text}, {"tone", lines_[i].tone}});
    return lines;
}

bool Hud::Notice(const std::string &text, const std::string &tone)
{
    if (!visible_ || text.empty() || !view_.log)
        return false;
    if (lines_.size() >= kLines)
        lines_.erase(lines_.begin());
    lines_.push_back({text, tone.empty() ? "info" : tone, mount_.Now() + kLineSeconds});
    changed_ = true;
    return true;
}

bool Hud::OnEvent(const Event &event)
{
    if (!visible_)
        return false;
    auto found = input_.handlers_.find(event.id + "\n" + event.verb);
    if (found == input_.handlers_.end())
        found = input_.handlers_.find(event.id + "\n");
    if (found == input_.handlers_.end())
    {
        Error("HUD scene " + std::string(entry_ ? entry_->name : "?") + ": event '" + event.id + " " + event.verb + "' has no binding");
        return false;
    }
    // The handler may ask for a rebuild, which replaces the handlers: keep this one alive.
    const Input::Handler handler = found->second;
    handler(event);
    return true;
}

// The platform host hears when the HUD appears and disappears: what it draws itself over a
// game without a HUD (a pause button) depends on it.
void Hud::Present(bool visible)
{
    if (visible == visible_)
        return;
    visible_ = visible;
    Send("presence", visible ? "shown" : "hidden");
}

void Hud::Refresh()
{
    changed_ = false;
    const SceneEntry *entry = nullptr;
    for (const SceneEntry &candidate : scenes_)
        for (const char *mode : candidate.modes)
            if (states_.gameplay.mode == mode)
                entry = &candidate;
    if (!entry && scene_ && states_.gameplay.mode == "unknown" && scene_->Holds(states_))
        entry = entry_;
    if (entry != entry_)
    {
        if (scene_)
        {
            scene_->Released(states_);
            scene_->Suspended(states_);
        }
        scene_.reset();
        entry_ = entry;
        if (entry_)
            scene_ = entry_->create(*this);
    }

    const Presentation &shown = states_.presentation;
    const bool live = shown.known && shown.live;
    if (scene_ && live_ && !live)
    {
        scene_->Released(states_);
        scene_->Suspended(states_);
    }
    live_ = live;

    // The raised telescope is drawn over any scene, and also where no scene is ported yet.
    const bool spyglass = states_.spyglass.known && states_.spyglass.on;
    const bool visible = (scene_ || spyglass) && shown.known && shown.screen.empty() && !shown.loading && !shown.cinematic && !shown.dialogue && !interface_;
    if (!visible)
    {
        Conceal();
        return;
    }

    // A blocked game ("unknown" mode) names no language: the last one stays.
    if (!states_.gameplay.language.empty() && states_.gameplay.language != language_)
    {
        language_ = states_.gameplay.language;
        mount_.SetLanguage(language_);
    }
    View view;
    Input input;
    Present(true); // Build may read LogLines and send input
    if (scene_)
        scene_->Build(states_, view, input);
    else
        view.layout = kSpyglassLayout;
    if (spyglass)
    {
        view.overlay = "s-hud-spyglass";
        view.overlay_props = SpyglassProps(states_.spyglass, *this);
    }
    input_ = std::move(input);
    if (!mount_.ShowHud(view))
    {
        // The pack cannot draw this view: reported by the mount; nothing half-drawn stays.
        Conceal();
        return;
    }
    view_ = std::move(view);
}

void Hud::Conceal()
{
    if (visible_)
    {
        if (scene_)
            scene_->Released(states_);
        mount_.HideHud();
    }
    Present(false);
    input_.handlers_.clear();
    lines_.clear();
    view_ = View();
    if (scene_)
        scene_->Hidden(states_);
}

double Hud::Tick()
{
    if (!declared_)
    {
        declared_ = true;
        for (const SceneEntry &entry : scenes_)
            if (entry.presents)
                Send("hudowner", entry.presents);
    }
    const double now = mount_.Now();
    const bool interface = mount_.InterfaceActive();
    if (interface != interface_)
    {
        interface_ = interface;
        changed_ = true;
    }
    const size_t before = lines_.size();
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [now](const Line &line) { return now >= line.until; }), lines_.end());
    if (lines_.size() != before)
        changed_ = true;
    // A handler or the scene's timed work may ask for another build: settle within the call.
    double next = kIdle;
    for (int pass = 0; pass < 4; ++pass)
    {
        if (changed_)
            Refresh();
        next = scene_ ? scene_->Tick(states_, now) : kIdle;
        if (!changed_)
            break;
    }
    if (visible_ && !view_.overlay.empty())
        next = std::min(next, std::max(0.0, spyglass_ack_at_ + kSpyglassHeartbeat - now));
    if (changed_)
        next = 0.0;
    for (const Line &line : lines_)
        next = std::min(next, std::max(0.0, line.until - now));
    return next;
}

void Hud::Drawn()
{
    if (visible_ && scene_ && !changed_)
        scene_->Drawn(states_);
    // The spyglass card is on the screen: the engine may hide its own info for this raise of
    // the telescope. Repeated while it stays; stopping lets the engine draw its own again.
    if (visible_ && !changed_ && !view_.overlay.empty() && states_.spyglass.target)
    {
        const double now = mount_.Now();
        if (states_.spyglass.generation != spyglass_ack_generation_ || now - spyglass_ack_at_ >= kSpyglassHeartbeat)
        {
            spyglass_ack_generation_ = states_.spyglass.generation;
            spyglass_ack_at_ = now;
            Send("spyglass", std::to_string(states_.spyglass.generation), "spyglass-ack");
        }
    }
}

void Hud::Released()
{
    if (scene_)
        scene_->Released(states_);
}

bool Hud::Back()
{
    return visible_ && scene_ && scene_->Back(states_);
}

// --- layout -----------------------------------------------------------------------------

namespace
{

bool ParseElement(const Json &source, LayoutElement &out, const std::string &where, std::string *error)
{
    const auto text = [&source](const char *key) {
        const auto found = source.find(key);
        return found != source.end() && found->is_string() ? found->get<std::string>() : std::string();
    };
    out.id = text("id");
    out.component = text("component");
    out.rcss = text("rcss");
    out.behavior = text("behavior");
    if (out.id.empty() || out.component.empty())
    {
        *error = where + ": an element needs 'id' and 'component'";
        return false;
    }
    for (const char *key : {"props", "data"})
    {
        const auto found = source.find(key);
        if (found == source.end())
            continue;
        if (!found->is_object())
        {
            *error = where + "/" + out.id + ": '" + key + "' must be an object";
            return false;
        }
        (std::string(key) == "props" ? out.props : out.data) = *found;
    }
    const auto children = source.find("children");
    if (children != source.end())
    {
        if (!children->is_array())
        {
            *error = where + "/" + out.id + ": 'children' must be a list";
            return false;
        }
        for (const Json &child : *children)
        {
            LayoutElement parsed;
            if (!child.is_object() || !ParseElement(child, parsed, where + "/" + out.id, error))
            {
                if (error->empty())
                    *error = where + "/" + out.id + ": a child is not an object";
                return false;
            }
            out.children.push_back(std::move(parsed));
        }
    }
    return true;
}

const LayoutElement *FindElement(const std::vector<LayoutElement> &elements, const std::string &id)
{
    for (const LayoutElement &element : elements)
    {
        if (element.id == id)
            return &element;
        if (const LayoutElement *child = FindElement(element.children, id))
            return child;
    }
    return nullptr;
}

} // namespace

bool ParseLayout(const Json &file, Layout &out, std::string *error)
{
    std::string problem;
    out.clear();
    const auto scenes = file.is_object() ? file.find("scenes") : file.end();
    if (!file.is_object() || file.value("version", 0) != 2 || scenes == file.end() || !scenes->is_object())
        problem = "hud-layout.json: expected version 2 with 'scenes'";
    else
        for (const auto &scene : scenes->items())
        {
            const auto elements = scene.value().is_object() ? scene.value().find("elements") : scene.value().end();
            if (!scene.value().is_object() || elements == scene.value().end() || !elements->is_array())
            {
                problem = "hud-layout.json: scene " + scene.key() + " has no 'elements'";
                break;
            }
            std::vector<LayoutElement> parsed;
            for (const Json &element : *elements)
            {
                LayoutElement item;
                if (!element.is_object() || !ParseElement(element, item, "hud-layout.json: " + scene.key(), &problem))
                {
                    if (problem.empty())
                        problem = "hud-layout.json: scene " + scene.key() + " holds something that is not an element";
                    break;
                }
                parsed.push_back(std::move(item));
            }
            if (!problem.empty())
                break;
            out[scene.key()] = std::move(parsed);
        }
    if (problem.empty())
        return true;
    out.clear();
    if (error)
        *error = problem;
    return false;
}

std::string CheckView(const Layout &layout, const View &view)
{
    const auto scene = layout.find(view.layout);
    if (scene == layout.end())
        return "HUD: the pack has no layout scene '" + view.layout + "'";
    if (!view.elements.is_object())
        return "HUD scene " + view.layout + ": the element props are not an object";
    for (const auto &item : view.elements.items())
    {
        if (!FindElement(scene->second, item.key()))
            return "HUD scene " + view.layout + ": the layout has no element '" + item.key() + "'";
        if (!item.value().is_object())
            return "HUD scene " + view.layout + ": the props of '" + item.key() + "' are not an object";
    }
    for (const std::string &id : view.dimmed)
        if (!FindElement(scene->second, id))
            return "HUD scene " + view.layout + ": the layout has no element '" + id + "' to dim";
    return {};
}

} // namespace storm::ui::hud
