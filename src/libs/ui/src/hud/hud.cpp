// The HUD core: which scene draws the current gameplay mode, when it is visible, and the
// way its events and timed work run. Scenes are in their own files (land.cpp).
#include "hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
constexpr double kLeaveSeconds = 0.22; // a line animates out this long (pack time.toastLeave), then goes
// The layout scene under the spyglass when no ported scene owns the mode (the platform still
// draws the sea HUD): the overlay alone, no elements.
constexpr const char *kSpyglassLayout = "sea";
// Where no scene draws the HUD: only the game-menu button (hud-layout.json scene "menu").
constexpr const char *kMenuLayout = "menu";

const char *const kLandModes[] = {"explore", "melee"};
const char *const kSeaModes[] = {"sailing", "naval"};
const char *const kWorldMapModes[] = {"worldmap"};

// Ported HUD scenes by gameplay mode. Porting a zone = adding its scene here; the
// platform host stops drawing that mode's HUD because HostOwnsHud turns false for it.
const SceneEntry kScenes[] = {
    {"land", kLandModes, CreateLandScene, "land"},
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

void BuildTimeScale(Hud &hud, const Gameplay &gameplay, double scale, View &view, Input &input)
{
    static const char *const kSteps[] = {"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"};
    if (scale < 0.0)
        return;
    for (const char *step : kSteps)
        if (!gameplay.Has(step))
            return;
    char text[16];
    std::snprintf(text, sizeof(text), "%.2f", scale);
    std::string badge = text;
    while (badge.back() == '0')
        badge.pop_back();
    if (badge.back() == '.')
        badge.pop_back();
    const int current = scale < 1.5 ? 0 : scale < 3.0 ? 1 : scale < 6.0 ? 2 : 3;
    view.elements["time"] = Json{{"badge", "×" + badge}, {"variant", current ? "active" : "normal"}};
    input.On("time", "click", [&hud, next = std::string(kSteps[(current + 1) % 4])](const Event &) {
        const Gameplay &now = hud.Current().gameplay;
        if (hud.Interactive() && now.Has(next))
            hud.Send("gameplay", std::to_string(now.revision) + "\t" + next);
    });
}

std::string Lower(std::string text)
{
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

void Hud::RequestSubject(const SubjectRequest &request)
{
    subject_ = request;
    subject_->until = Now() + kSubjectSeconds;
}

std::optional<SubjectRequest> Hud::TakeSubject(const std::string &screen)
{
    if (!subject_ || subject_->screen != screen)
        return std::nullopt;
    std::optional<SubjectRequest> taken;
    if (Now() <= subject_->until)
        taken = subject_;
    else
        Log("HUD: the " + screen + " screen opened after its subject request lapsed; it shows its own first subject");
    subject_.reset();
    return taken;
}

Json MemberCell(const std::string &name, const Picture &picture, std::initializer_list<MemberBar> bars, bool tap)
{
    Json cell = {{"name", name}, {"texture", picture.texture}, {"uv", picture.uv}, {"bars", Json::array()}};
    for (const MemberBar &bar : bars)
        if (bar.share >= 0.0)
            cell["bars"].push_back(Json{{"kind", bar.kind}, {"value", static_cast<int>(std::lround(std::clamp(bar.share, 0.0, 1.0) * 100.0))}});
    if (tap)
        cell["tap"] = true;
    return cell;
}

std::string ReadPicture(const Json &value, Picture &out)
{
    const auto texture = value.is_object() ? value.find("texture") : value.end();
    const auto uv = value.is_object() ? value.find("uv") : value.end();
    if (texture == value.end() || !texture->is_string() || uv == value.end() || !uv->is_array() || uv->size() != 4)
        return "picture: expected {texture, uv: [left, top, right, bottom]}";
    const std::string name = texture->get<std::string>();
    if (name.empty() || name.size() > 128 || name.find_first_of("\\:\"") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return "picture: bad texture name";
    double rect[4];
    for (size_t i = 0; i < 4; ++i)
    {
        if (!(*uv)[i].is_number())
            return "picture: uv is not four numbers";
        rect[i] = (*uv)[i].get<double>();
        if (!std::isfinite(rect[i]) || rect[i] < 0.0 || rect[i] > 1.0)
            return "picture: uv outside 0..1";
    }
    if (rect[2] <= rect[0] || rect[3] <= rect[1])
        return "picture: empty uv rectangle";
    char text[96];
    std::snprintf(text, sizeof text, "%.6g,%.6g,%.6g,%.6g", rect[0], rect[1], rect[2], rect[3]);
    out = {name, text};
    return {};
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
        lines.push_back(Json{{"text", lines_[i].text}, {"tone", lines_[i].tone}, {"key", std::to_string(lines_[i].key)}, {"leaving", lines_[i].leaving >= 0.0}});
    return lines;
}

bool Hud::Notice(const std::string &text, const std::string &tone)
{
    if (!visible_ || text.empty() || !view_.log)
        return false;
    for (Line &line : lines_)
        if (line.base == text && line.leaving < 0.0)
        {
            line.text = RepeatedText(text, ++line.count);
            line.until = mount_.Now() + kLineSeconds;
            changed_ = true;
            return true;
        }
    // At most kLines shown: the oldest shown line starts to leave.
    const double now = mount_.Now();
    if (std::count_if(lines_.begin(), lines_.end(), [](const Line &line) { return line.leaving < 0.0; }) >= static_cast<long>(kLines))
        for (Line &line : lines_)
            if (line.leaving < 0.0)
            {
                line.leaving = now;
                break;
            }
    lines_.push_back({text, tone.empty() ? "info" : tone, now + kLineSeconds, text, 1, ++next_line_key_});
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
void Hud::Present(bool visible, bool menu_only)
{
    if (visible == visible_ && menu_only == menu_only_)
        return;
    visible_ = visible;
    menu_only_ = menu_only;
    Send("presence", !visible ? "hidden" : menu_only ? "menu" : "shown");
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
    // Over a free game the HUD is up; where no scene draws it (a cut-scene, a mode no scene
    // owns: a blocked game) the layout "menu" still gives the one game-menu button, as every
    // scene has it. The game decides what its menu offers there (save disabled and why).
    const bool free = shown.known && shown.screen.empty() && !shown.loading && !shown.dialogue && !interface_;
    const bool full = free && (scene_ || spyglass) && !shown.cinematic;
    if (!free)
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
    if (!full && visible_ && !menu_only_ && scene_)
    {
        // The scene's HUD gives way to the menu button (a cut-scene starts): as when hidden.
        scene_->Released(states_);
        scene_->Hidden(states_);
    }
    Present(true, !full); // Build may read LogLines and send input
    if (!full)
    {
        view.layout = kMenuLayout;
        view.elements["pause"] = Json::object();
        input.On("pause", "click", [this](const Event &) { Send("shortcut", "mainmenu"); });
    }
    else if (scene_)
        scene_->Build(states_, view, input);
    else
        view.layout = kSpyglassLayout;
    if (full && spyglass)
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
    Present(false, false);
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
        Send("hudowner", "spyglass"); // the Hud lays the spyglass overlay over any scene
    }
    const double now = mount_.Now();
    const bool interface = mount_.InterfaceActive();
    if (interface != interface_)
    {
        interface_ = interface;
        changed_ = true;
    }
    // An expired line animates out first, then goes.
    for (Line &line : lines_)
        if (line.leaving < 0.0 && now >= line.until)
        {
            line.leaving = now;
            changed_ = true;
        }
    const size_t before = lines_.size();
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [now](const Line &line) { return line.leaving >= 0.0 && now >= line.leaving + kLeaveSeconds; }),
                 lines_.end());
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
    if (changed_)
        next = 0.0;
    for (const Line &line : lines_)
        next = std::min(next, std::max(0.0, (line.leaving >= 0.0 ? line.leaving + kLeaveSeconds : line.until) - now));
    return next;
}

void Hud::Drawn()
{
    if (visible_ && !menu_only_ && scene_ && !changed_)
        scene_->Drawn(states_);
    // The spyglass overlay is on the screen, or not: said once per raise of the telescope (and
    // again when that changes); the engine keeps the word for the raise (android_spyglass.hpp).
    const Spyglass &spyglass = states_.spyglass;
    if (!spyglass.known || !spyglass.on)
    {
        spyglass_answer_.clear();
        return;
    }
    if (changed_)
        return; // the view is not settled yet: answer for what is drawn
    const bool covering = visible_ && !view_.overlay.empty();
    const std::string answer = std::to_string(spyglass.generation) + "\t" + (covering ? "1" : "0");
    if (answer == spyglass_answer_)
        return;
    spyglass_answer_ = answer;
    Send("spyglass", answer, "spyglass-ack");
}

void Hud::Released()
{
    if (scene_)
        scene_->Released(states_);
}

bool Hud::Back()
{
    return visible_ && !menu_only_ && scene_ && scene_->Back(states_);
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
