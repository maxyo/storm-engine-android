// Screens: C++ code that turns an InterfaceModel into the props of a pack screen
// (an <s-…> component of mobile-ui/design/screens) and component events back into
// engine actions. See README.md in this directory for how to add one.
#pragma once

#include "../template.h"
#include "storm_ui/interface_model.h"
#include "storm_ui/ui.h"

#include <functional>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui::hud
{
class Hud; // ../hud/hud.h
}

namespace storm::ui::screens
{

// Marks a UI string key kept in a table and looked up later with Bindings::Text, so the
// pack build (mobile-ui/design/build.py) can check that the key is defined.
constexpr const char *StringKey(const char *key)
{
    return key;
}

// One entry of a screen's KNOWN list: a node it draws by design and the widget type it
// expects there. `required` marks the REQUIRED subset. A node ending in '*' matches by prefix.
struct NodeRule
{
    const char *node;
    const char *type;
    bool required = false;
};

// What a component reported: the id of the nearest element that has one, the verb and value.
struct Event
{
    std::string id;
    std::string verb;
    std::string value;
};

// Never shown anywhere (GENERIC-LAYOUT §7): scroll bars, backgrounds, empty and debug nodes.
bool Hidden(const Widget &widget);

// Result of admission (GENERIC-LAYOUT §8).
struct Admission
{
    const InterfaceModel *model = nullptr;
    bool accepted = false;               // every REQUIRED node is present with its type
    std::string missing;                 // REQUIRED nodes that are absent or mistyped: "TABLE_LIST(choice), …"
    std::vector<const Widget *> known;   // KNOWN node with the expected type
    std::vector<const Widget *> extras;  // everything else that is not hidden

    const Widget *One(std::string_view node) const;
    std::vector<const Widget *> All(std::string_view node) const;
    std::string Label(std::string_view node) const;
};
Admission Admit(const InterfaceModel &model, std::span<const NodeRule> rules);

class Router;

// Given to Screen::Build: connects element ids of the view to what happens on their events.
class Bindings
{
  public:
    using Handler = std::function<void(const Event &)>;

    // Runs `handler` for events of the element `id`; an empty verb takes every verb.
    void On(const std::string &id, const std::string &verb, Handler handler);
    // Events of `id` with this verb are expected and need no action (drag progress, …).
    // An event that is neither bound nor ignored is reported as an error.
    void Ignore(const std::string &id, const std::string &verb);
    // An event of `id` sends this verb of the widget to the engine.
    void Forward(const std::string &id, const Widget &widget, const std::string &verb);

    // --- inside handlers ---
    // Sends a widget verb to the engine. `value` is the row identity or the new value.
    void Send(const Widget &widget, const std::string &verb, const std::string &value = {}, const std::string &guard = {});
    // The same, but a newer value for the same widget and verb replaces one still waiting
    // (drags): at most one goes out per flush interval.
    void SendLatest(const Widget &widget, const std::string &verb, const std::string &value, const std::string &guard = {});
    // Sends after `seconds` unless a newer SendAfter for the same widget and verb arrives
    // first, which replaces it and restarts the wait (settling a drag before it applies).
    void SendAfter(double seconds, const Widget &widget, const std::string &verb, const std::string &value = {},
                   const std::string &guard = {});
    // A setting of the application itself, not of the game (renderer options): an Action
    // without a screen that the platform host keeps (the Android host: GraphicsSettings).
    void SendHost(const std::string &node, const std::string &value);
    // The player's way out: the screen's exit button when it has one, else the global back.
    void Back(const Widget *exit_button);
    // Opens the native text editor over the field; the accepted text is sent as `edit`.
    void EditText(const Event &event, const Widget &widget, const std::string &title, bool numeric, bool secret = false);
    // The same editor, but the accepted text goes to `accepted` (the screen checks it, keeps
    // a draft, sends it itself); nothing happens when the player cancels.
    void AskText(const Event &event, const std::string &text, const std::string &title, bool numeric,
                 std::function<void(const std::string &)> accepted);
    // A request for the platform host, not the engine (Action screen "hud", like the HUD's
    // `shortcut`): `pause` opens the host's pause menu.
    void Platform(const std::string &verb, const std::string &value = {});
    // Shows a long text on a parchment sheet over the screen.
    void Read(const std::string &title, const std::string &text);
    void Toast(const std::string &text);
    // Something the screen cannot do as designed: an error line (a toast in debug hosts).
    void Error(const std::string &text);
    // Local state of the screen changed: build the view again from the same model.
    void Rebuild();
    // Builds the view again after `seconds` even if no new model comes (local state that
    // expires, such as the amount the player set being preferred over the game's for a moment).
    void RebuildAfter(double seconds);

    // The UI string `key` of the pack's string table (mobile-ui/design/strings) in the
    // language of the open interface. Every text the player reads that the game does not
    // send comes from there, never from a literal.
    std::string Text(const std::string &key) const;

    // --- inside Build ---
    // True from the moment an event of `id` (and `value`, for rows) sent something to the
    // engine until the next model revision (R22).
    bool Pending(const std::string &id, const std::string &value = {}) const;
    double Now() const;

  private:
    friend class Router;
    explicit Bindings(Router &router) : router_(router)
    {
    }
    Router &router_;
    std::unordered_map<std::string, Handler> handlers_;
};

class Screen
{
  public:
    virtual ~Screen() = default;
    // Tag of the pack screen this implementation fills ("s-store").
    virtual const char *Component() const = 0;
    // KNOWN and REQUIRED nodes. Empty for the generic layout, which takes every widget.
    virtual std::span<const NodeRule> Nodes() const = 0;
    // Maps the admitted widgets to the props of the pack screen and binds its elements.
    // Called for every model revision and after Bindings::Rebuild(); the instance lives
    // while the same screen stays open, so members hold local state (filters, open sheets).
    virtual Json Build(const Admission &admitted, Bindings &bindings) = 0;
    // True while the screen shows something of its own that the back key closes (a local
    // modal); Back is then called instead of the engine's back.
    virtual bool OwnsBack() const
    {
        return false;
    }
    virtual void Back(Bindings &)
    {
    }
};

struct ScreenEntry
{
    const char *id; // interface id
    std::unique_ptr<Screen> (*create)();
};
// Screens ported to C++, keyed by interface id (registry.cpp).
std::span<const ScreenEntry> PortedScreens();
std::unique_ptr<Screen> CreateGenericScreen();

// A named state channel of the game (gameplay, combat, dialogue, …): consumers get the
// engine's JSON for it. Zone authors register one consumer per channel in registry.cpp.
class StateConsumer
{
  public:
    virtual ~StateConsumer() = default;
    virtual const char *Channel() const = 0;
    virtual void OnState(const Json &state, Router &router) = 0;
};
std::vector<std::unique_ptr<StateConsumer>> CreateStateConsumers();

// What the router needs from the host; unit tests give it a fake.
class Surface
{
  public:
    virtual ~Surface() = default;
    // Mounts the pack screen `tag` (or keeps it when already mounted) and sets its props.
    virtual void Show(const std::string &tag, const Json &props) = 0;
    virtual void Hide() = 0;
    virtual void Send(const Action &action) = 0;
    // `anchor` is the id of the element the editor belongs to; the host fills the rectangle.
    virtual void RequestText(TextRequest request, const std::string &anchor) = 0;
    virtual void Toast(const std::string &text) = 0;
    // The game language of the interface being shown ("russian"); tr() in templates and
    // Text() follow it.
    virtual void SetLanguage(const std::string &game_language) = 0;
    virtual std::string Text(const std::string &key) = 0;
    virtual void Log(const std::string &line) = 0;
    // Something is wrong with the data or the pack: an error log line, and in debug hosts
    // a visible notice. Never a quiet drop.
    virtual void Error(const std::string &line) = 0;
    virtual double Now() const = 0;
};

class Router
{
  public:
    explicit Router(Surface &surface);
    ~Router();

    void SetInterface(const InterfaceModel &model);
    void CloseInterface();
    void SetState(const std::string &channel, const Json &state);
    // False when no screen is mounted or the event belongs to nothing bound.
    bool OnEvent(const Event &event);
    void SubmitText(const std::string &token, const std::string &text, bool accepted);
    // True while the screen has a modal of its own (Screen::OwnsBack): the back key is the UI's.
    bool WantsBack() const;
    // The platform's back: the screen's own modal first, then the reading or «Прочее» sheet,
    // then what the screen's ← does, the engine's back when the screen binds no ←.
    void Back();
    // Flushes throttled sends and expired pending marks; returns seconds until it must be
    // called again (a large number when idle).
    double Tick();
    // The HUD that the HUD channel consumers fill; null in a host without one.
    void AttachHud(hud::Hud *hud)
    {
        hud_ = hud;
    }
    hud::Hud *Hud() const
    {
        return hud_;
    }
    // For consumers: something is wrong with the data of their channel.
    void ReportError(const std::string &line);
    bool Active() const
    {
        return screen_ != nullptr;
    }
    // Name of the screen implementation in use: the interface id or "generic".
    const std::string &Using() const
    {
        return using_;
    }
    // Why the ported screen of this interface id was not used (its REQUIRED nodes that are
    // missing or mistyped); empty when the screen in use is the intended one.
    const std::string &Rejection() const
    {
        return rejection_;
    }

  private:
    friend class Bindings;
    struct PendingMark
    {
        std::string id, value;
        uint64_t revision = 0;
        double until = 0.0;
    };
    struct Waiting
    {
        Action action;
        double due = 0.0;
    };

    void Build();
    void Emit(Action action);
    Json ExtrasView(const Admission &admitted, Json rows);
    Action Make(const Widget &widget, const std::string &verb, const std::string &value, const std::string &guard) const;

    Surface &surface_;
    hud::Hud *hud_ = nullptr;
    InterfaceModel model_;
    std::unique_ptr<Screen> screen_;
    std::string using_;
    Bindings bindings_;
    std::vector<std::unique_ptr<StateConsumer>> consumers_;
    std::vector<PendingMark> pending_;
    std::vector<Waiting> waiting_;
    std::set<std::string> logged_; // once per (screen, node set): extras, generic use, rejections
    std::string rejection_;
    // Shared «Прочее» sheet and reading sheet.
    bool extras_open_ = false;
    std::string reading_title_, reading_text_;
    // Text editor in flight.
    std::string edit_token_;
    Action edit_action_;
    std::function<void(const std::string &)> edit_handler_; // Bindings::AskText
    int edit_serial_ = 0;
    // Event being handled: sends made by its handler mark it pending.
    const Event *current_ = nullptr;
    bool sent_in_event_ = false;
    bool rebuild_ = false;
    double last_flush_ = -1.0;
    double rebuild_at_ = 0.0;   // Bindings::RebuildAfter
    double opened_at_ = 0.0;    // when the open interface was opened
    double settle_until_ = 0.0; // a rejected first model waits until then (kSettle)
};

} // namespace storm::ui::screens
