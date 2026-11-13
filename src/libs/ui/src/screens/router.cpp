#include "controls.h"

#include "../hud/hud.h"

#include <algorithm>

namespace storm::ui
{

std::string EngineCommand(const Action &action)
{
    if (action.screen.empty())
        return {};
    if (action.screen == "sound") // the interface click of a press that started (Services::PressStarted)
        return "uisound:" + action.verb;
    if (action.screen == "hud") // HUD input: the verb names the engine entry point
        return action.verb + ":" + action.value;
    if (action.screen == "dialogue") // reply: "<generation>:<index>"; edit: the typed reply text; no back
        return action.verb == "back" ? std::string()
             : action.verb == "edit" ? "dialogue:" + std::to_string(action.opening) + ":" + std::to_string(action.index) + ":" + action.value
                                     : "dialogue:" + action.value;
    if (action.screen == "mainmenu") // the title screen's commands: "main:New", "main:Load", …
        return action.verb == "back" ? "back" : "main:" + action.node;
    if (action.node.empty())
        return action.verb == "back" ? "back" : std::string();
    const std::string stamp = "@" + std::to_string(action.opening) + ":";
    std::string wire;
    if (action.verb == "toggle" || action.verb == "edit")
        wire = action.value;
    else if (action.verb == "set" || !action.value.empty())
        wire = action.verb + stamp + action.value;
    else
        wire = action.verb;
    if (!action.guard.empty())
        wire = "guard" + stamp + action.guard + "|" + wire;
    return "widget:" + action.screen + "\t" + action.node + "\t" + std::to_string(action.index) + "\t" + wire;
}

bool HostOwnsHud(const std::string &mode)
{
    return !hud::Ported(mode);
}

} // namespace storm::ui

namespace storm::ui::screens
{

namespace
{

constexpr double kPendingSeconds = 1.5; // a press the engine never answers stops looking busy
constexpr double kFlushSeconds = 0.12;  // drags reach the engine at most this often
constexpr double kIdle = 3600.0;
// The game publishes an interface while its script is still filling it (the store's first
// revision has no goods yet). For this long after an opening a rejected model is not drawn
// and not reported; a newer revision that passes admission shows the ported screen.
constexpr double kSettle = 1.0;

bool Contains(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

bool Matches(const NodeRule &rule, const Widget &widget)
{
    const std::string_view pattern = rule.node;
    const bool prefix = !pattern.empty() && pattern.back() == '*';
    const std::string_view name = prefix ? pattern.substr(0, pattern.size() - 1) : pattern;
    if (prefix ? !(widget.node.size() >= name.size() && SameNode(std::string_view(widget.node).substr(0, name.size()), name))
               : !SameNode(widget.node, name))
        return false;
    return true;
}

bool TypeFits(const NodeRule &rule, const Widget &widget)
{
    std::string_view types = rule.type;
    while (!types.empty())
    {
        const size_t bar = types.find('|');
        const std::string_view one = types.substr(0, bar);
        if (one == "*" || one == widget.type)
            return true;
        types = bar == std::string_view::npos ? std::string_view() : types.substr(bar + 1);
    }
    return false;
}

} // namespace

bool Hidden(const Widget &widget)
{
    const std::string node = Upper(widget.node);
    const bool has_verbs = !widget.actions.empty();
    if (node.rfind("DEBUG", 0) == 0 || (node.size() > 4 && node.compare(node.size() - 4, 4, "_DBG") == 0))
        return true;
    if (Contains(node, "SCROLL") && !has_verbs && widget.type != "choice" && widget.type != "text")
        return true;
    if (widget.type == "image")
        return widget.label.empty() || !widget.Offers("press"); // pictures are drawn only by screens that know them
    if (widget.type == "edit" || widget.type == "slider" || widget.type == "stepper")
        return false;
    if (widget.type == "choice" && (!widget.cells.empty() || !widget.images.empty()))
        return false;
    if (widget.label.empty() && widget.images.empty())
        return true;
    if (widget.type == "button" && !widget.enabled && widget.unavailable_reason.empty() && !has_verbs && widget.label.empty())
        return true;
    // Form-specific types mean something only to their form; elsewhere they need a verb.
    if (widget.type == "save" || widget.type.rfind("nation_help", 0) == 0)
        return !has_verbs;
    return false;
}

const Widget *Admission::One(std::string_view node) const
{
    for (const Widget *widget : known)
        if (SameNode(widget->node, node))
            return widget;
    return nullptr;
}

std::vector<const Widget *> Admission::All(std::string_view node) const
{
    std::vector<const Widget *> out;
    for (const Widget *widget : known)
        if (SameNode(widget->node, node))
            out.push_back(widget);
    return out;
}

std::vector<const Widget *> Admission::Rows(std::string_view node) const
{
    std::vector<const Widget *> out;
    for (const Widget *widget : known)
        if (SameNode(widget->node, node) && widget->type == "choice")
            out.push_back(widget);
    return out;
}

std::string Admission::Label(std::string_view node) const
{
    const Widget *widget = One(node);
    return widget ? widget->label : std::string();
}

Admission Admit(const InterfaceModel &model, std::span<const NodeRule> rules)
{
    Admission out;
    out.model = &model;
    std::vector<bool> met(rules.size(), false);
    for (const Widget &widget : model.widgets)
    {
        bool known = false, named = false;
        for (size_t i = 0; i < rules.size(); ++i)
        {
            if (!Matches(rules[i], widget))
                continue;
            named = true;
            if (TypeFits(rules[i], widget))
            {
                known = true;
                met[i] = true;
                break;
            }
        }
        if (known)
            out.known.push_back(&widget);
        else if (!Hidden(widget))
        {
            // An alias ("NAME_2") of a node the screen draws is not an extra.
            const std::string node = Upper(widget.node);
            const bool alias = named || (node.size() > 2 && node.compare(node.size() - 2, 2, "_2") == 0 &&
                                         std::any_of(rules.begin(), rules.end(), [&](const NodeRule &rule) {
                                             return SameNode(rule.node, std::string_view(node).substr(0, node.size() - 2));
                                         }));
            if (!alias || named)
                out.extras.push_back(&widget);
        }
    }
    out.accepted = true;
    for (size_t i = 0; i < rules.size(); ++i)
        if (rules[i].required && !met[i])
        {
            out.accepted = false;
            out.missing += (out.missing.empty() ? "" : ", ") + std::string(rules[i].node) + "(" + rules[i].type + ")";
        }
    return out;
}

// --- Bindings ---------------------------------------------------------------------------

void Bindings::On(const std::string &id, const std::string &verb, Handler handler)
{
    handlers_[id + '\x1f' + verb] = std::move(handler);
}

void Bindings::Ignore(const std::string &id, const std::string &verb)
{
    On(id, verb, [](const Event &) {});
}

void Bindings::Forward(const std::string &id, const Widget &widget, const std::string &verb)
{
    const Widget *target = &widget;
    On(id, "", [this, target, verb](const Event &) { Send(*target, verb); });
}

void Bindings::Send(const Widget &widget, const std::string &verb, const std::string &value, const std::string &guard)
{
    router_.Emit(router_.Make(widget, verb, value, guard));
}

void Bindings::SendLatest(const Widget &widget, const std::string &verb, const std::string &value, const std::string &guard,
                          const std::optional<std::string> &lead)
{
    Action action = router_.Make(widget, verb, value, guard);
    std::optional<Action> first;
    if (lead)
        first = router_.Make(widget, verb, *lead, guard);
    // A send that waits for its turn still changed what the screen shows (the amount the
    // player just set): the view is built again now, not when it goes out.
    router_.rebuild_ = true;
    for (Router::Waiting &waiting : router_.waiting_)
        if (waiting.action.node == action.node && waiting.action.index == action.index && waiting.action.verb == action.verb)
        {
            waiting.action = std::move(action);
            waiting.lead = std::move(first);
            return;
        }
    const double now = router_.surface_.Now();
    if (now - router_.last_flush_ >= kFlushSeconds)
    {
        router_.last_flush_ = now;
        if (first)
            router_.Emit(std::move(*first));
        router_.Emit(std::move(action));
        return;
    }
    router_.waiting_.push_back({std::move(action), router_.last_flush_ + kFlushSeconds, std::move(first)});
}

void Bindings::SendAfter(double seconds, const Widget &widget, const std::string &verb, const std::string &value, const std::string &guard,
                         const std::optional<std::string> &lead)
{
    Action action = router_.Make(widget, verb, value, guard);
    std::optional<Action> first;
    if (lead)
        first = router_.Make(widget, verb, *lead, guard);
    const double due = router_.surface_.Now() + seconds;
    router_.rebuild_ = true; // as SendLatest: the view shows what was asked at once
    for (Router::Waiting &waiting : router_.waiting_)
        if (waiting.action.node == action.node && waiting.action.index == action.index && waiting.action.verb == action.verb)
        {
            waiting.action = std::move(action);
            waiting.lead = std::move(first);
            waiting.due = due;
            return;
        }
    router_.waiting_.push_back({std::move(action), due, std::move(first)});
}

bool Bindings::Waiting(const Widget &widget, const std::string &verb) const
{
    for (const Router::Waiting &waiting : router_.waiting_)
        if (waiting.action.node == widget.node && waiting.action.index == widget.index && waiting.action.verb == verb)
            return true;
    return false;
}

void Bindings::SendHost(const std::string &node, const std::string &value)
{
    Action action;
    action.node = node;
    action.verb = "set";
    action.value = value;
    router_.Emit(std::move(action));
}

void Bindings::Back(const Widget *exit_button)
{
    if (exit_button && exit_button->Offers("press"))
    {
        Send(*exit_button, "press");
        return;
    }
    Action action;
    action.screen = router_.model_.screen;
    action.opening = router_.model_.opening;
    action.verb = "back";
    router_.Emit(std::move(action));
}

void Bindings::EditText(const Event &event, const Widget &widget, const std::string &title, bool numeric, bool secret)
{
    TextRequest request;
    request.token = std::to_string(++router_.edit_serial_);
    request.text = widget.value;
    request.title = title;
    request.numeric = numeric;
    request.secret = secret;
    router_.edit_token_ = request.token;
    router_.edit_action_ = router_.Make(widget, "edit", {}, {});
    router_.edit_handler_ = nullptr;
    router_.surface_.RequestText(std::move(request), event.id);
}

void Bindings::AskText(const Event &event, const std::string &text, const std::string &title, bool numeric,
                       std::function<void(const std::string &)> accepted)
{
    TextRequest request;
    request.token = std::to_string(++router_.edit_serial_);
    request.text = text;
    request.title = title;
    request.numeric = numeric;
    router_.edit_token_ = request.token;
    router_.edit_action_ = Action();
    router_.edit_action_.screen = router_.model_.screen;
    router_.edit_action_.opening = router_.model_.opening;
    router_.edit_handler_ = std::move(accepted);
    router_.surface_.RequestText(std::move(request), event.id);
}

void Bindings::Read(const std::string &title, const std::string &text)
{
    router_.reading_title_ = title;
    router_.reading_text_ = text;
    router_.rebuild_ = true;
}

void Bindings::Toast(const std::string &text)
{
    router_.surface_.Toast(text);
}

Json Bindings::Notice(Json notice)
{
    const std::string tone = notice.value("tone", std::string("info"));
    std::string text = notice.value("text", std::string());
    if (notice.contains("lead") && notice["lead"].is_string())
        text = notice["lead"].get<std::string>() + " " + text;
    Shown(tone, text);
    return notice;
}

void Bindings::Shown(const std::string &tone, const std::string &text)
{
    const std::string key = router_.using_ + "\n" + tone + "\n" + text;
    if (!text.empty() && router_.notices_building_.insert(key).second && !router_.notices_shown_.count(key))
        router_.surface_.Shown("notice on screen " + router_.using_, tone, text);
}

void Bindings::Error(const std::string &text)
{
    router_.surface_.Error("screen " + router_.using_ + ": " + text);
}

std::optional<SubjectRequest> Bindings::TakeSubject(const std::string &screen)
{
    return router_.hud_ ? router_.hud_->TakeSubject(screen) : std::nullopt;
}

void Bindings::Rebuild()
{
    router_.rebuild_ = true;
}

void Bindings::RebuildAfter(double seconds)
{
    const double when = router_.surface_.Now() + std::max(0.0, seconds);
    if (router_.rebuild_at_ == 0.0 || when < router_.rebuild_at_)
        router_.rebuild_at_ = when;
}

std::string Bindings::Text(const std::string &key) const
{
    return router_.surface_.Text(key);
}

bool Bindings::Pending(const std::string &id, const std::string &value) const
{
    for (const Router::PendingMark &mark : router_.pending_)
        if (mark.id == id && mark.value == value)
            return true;
    return false;
}

double Bindings::Now() const
{
    return router_.surface_.Now();
}

// --- Router -----------------------------------------------------------------------------

Router::Router(Surface &surface) : surface_(surface), bindings_(*this), consumers_(CreateStateConsumers())
{
}

Router::~Router() = default;

Action Router::Make(const Widget &widget, const std::string &verb, const std::string &value, const std::string &guard) const
{
    Action action;
    action.screen = model_.screen;
    action.opening = model_.opening;
    action.node = widget.node;
    action.index = widget.index;
    action.verb = verb;
    action.value = value;
    action.guard = guard;
    return action;
}

void Router::Emit(Action action)
{
    if (current_)
        sent_in_event_ = true;
    surface_.Send(action);
}

void Router::SetInterface(const InterfaceModel &model)
{
    const bool reopened = (!screen_ && settle_until_ == 0.0) || model.screen != model_.screen || model.opening != model_.opening;
    if (model.language != model_.language || !screen_)
        surface_.SetLanguage(model.language);
    const bool changed = model.revision != model_.revision;
    model_ = model;
    if (reopened)
    {
        opened_at_ = surface_.Now();
        screen_.reset();
        extras_open_ = false;
        reading_title_.clear();
        reading_text_.clear();
        pending_.clear();
        waiting_.clear();
        edit_token_.clear();
        edit_handler_ = nullptr;
    }
    else if (changed)
        pending_.clear();
    Build();
}

void Router::CloseInterface()
{
    if (!screen_ && settle_until_ == 0.0)
        return;
    settle_until_ = 0.0;
    screen_.reset();
    using_.clear();
    rejection_.clear();
    bindings_.handlers_.clear();
    pending_.clear();
    waiting_.clear();
    edit_token_.clear();
    edit_handler_ = nullptr;
    model_ = InterfaceModel();
    surface_.Hide();
}

void Router::CloseGameInterface(const std::string &screen)
{
    if (!model_.screen.empty() && model_.screen != screen)
    {
        surface_.Log("interface " + (screen.empty() ? std::string("(not modelled yet)") : screen) + " closed under the " + model_.screen +
                     " screen: that screen stays");
        return;
    }
    CloseInterface();
    for (const auto &consumer : consumers_)
        consumer->Resume(*this);
}

void Router::SetState(const std::string &channel, const Json &state)
{
    bool consumed = false;
    for (const auto &consumer : consumers_)
        if (channel == consumer->Channel())
        {
            consumer->OnState(state, *this);
            consumed = true;
        }
    if (!consumed)
        surface_.Error("state channel " + channel + " has no consumer");
}

void Router::ReportError(const std::string &line)
{
    surface_.Error(line);
}

void Router::Alert(const std::string &key, const std::string &subject)
{
    surface_.Alert(subject.empty() ? surface_.Text(key) : surface_.Text(key) + ": " + subject);
}

void Router::Build()
{
    notices_shown_ = std::move(notices_building_);
    notices_building_.clear();
    rebuild_ = false;
    rebuild_at_ = 0.0;
    bindings_.handlers_.clear();
    // The ported screen registered for the id draws it when the model passes its admission.
    // If its REQUIRED nodes are missing or mistyped the interface is still drawn, by the
    // generic layout, and that is reported as an error (never a quiet switch).
    const ScreenEntry *ported = nullptr;
    for (const ScreenEntry &entry : PortedScreens())
        if (model_.screen == entry.id)
            ported = &entry;
    Admission admitted;
    std::string rejection;
    if (ported)
    {
        if (using_ != ported->id || !screen_)
        {
            screen_ = ported->create();
            using_ = ported->id;
        }
        admitted = Admit(model_, screen_->Nodes());
        if (!admitted.accepted)
        {
            rejection = admitted.missing;
            screen_.reset();
            if (surface_.Now() - opened_at_ < kSettle)
            {
                settle_until_ = opened_at_ + kSettle; // Tick builds again then
                using_.clear();
                surface_.Hide();
                return;
            }
        }
    }
    settle_until_ = 0.0;
    if (!ported || !rejection.empty())
    {
        if (using_ != "generic" || !screen_)
        {
            screen_ = CreateGenericScreen();
            using_ = "generic";
        }
        admitted = Admit(model_, screen_->Nodes());
    }
    rejection_ = rejection;
    std::string nodes;
    for (const Widget &widget : model_.widgets)
        if (nodes.find("," + widget.node + ",") == std::string::npos)
            nodes += (nodes.empty() ? "," : "") + widget.node + ",";
    if (!rejection.empty())
    {
        if (logged_.insert("rejected:" + model_.screen + ":" + nodes).second)
            surface_.Error("screen " + model_.screen + ": required nodes missing or mistyped: " + rejection + "; drawn by the generic layout");
    }
    else if (!ported && logged_.insert("generic:" + model_.screen + ":" + nodes).second)
        surface_.Log("screen " + model_.screen + ": no ported screen, generic layout; nodes=" + (nodes.empty() ? std::string("none") : nodes.substr(1, nodes.size() - 2)));
    Json view = screen_->Build(admitted, bindings_);
    // The generic layout places every widget itself and hands over only its overflow rows.
    Json rows = view.contains("more") ? view["more"] : Json::array();
    view.erase("more");
    view["extras"] = ExtrasView(admitted, std::move(rows));
    surface_.Show(screen_->Component(), view);
}

Json Router::ExtrasView(const Admission &admitted, Json rows)
{
    std::string note, nodes;
    std::vector<const Widget *> controls;
    const auto flush = [&] {
        if (!controls.empty())
            ControlRows(bindings_, controls, "extra-c" + std::to_string(rows.size()) + "-", std::vector<std::string>(), rows);
        controls.clear();
    };
    const bool generic = screen_->Nodes().empty();
    for (const Widget *widget : admitted.extras)
    {
        if (generic)
            break;
        nodes += (nodes.empty() ? "" : ",") + widget->node;
        if (widget->type == "text" || widget->type == "heading")
        {
            note += (note.empty() ? "" : "\n") + widget->label;
            continue;
        }
        if (widget->type == "check" || widget->type == "edit" || widget->type == "slider" || widget->type == "stepper")
        {
            controls.push_back(widget);
            continue;
        }
        flush();
        if (widget->type == "button" || widget->type == "choice" || widget->type == "image")
            rows.push_back(ActionRow(bindings_, *widget, "extra-a" + std::to_string(rows.size())));
    }
    flush();
    if (!nodes.empty() && logged_.insert("extras:" + model_.screen + ":" + nodes).second)
        surface_.Log("screen " + model_.screen + " extras=" + nodes);

    Json view = Json::object();
    view["rows"] = rows;
    view["open"] = extras_open_ && !rows.empty();
    view["actions"] = rows.empty() ? Json::array() : Json::array({{{"id", "extras-more"}, {"icon", "more"}, {"label", surface_.Text("common.other")}}});
    view["title"] = surface_.Text("common.other");
    view["note"] = note;
    view["reading"] = !reading_text_.empty();
    view["readingTitle"] = reading_title_;
    view["readingText"] = reading_text_;
    bindings_.On("extras-more", "", [this](const Event &) {
        extras_open_ = !extras_open_;
        rebuild_ = true;
    });
    bindings_.On("extras", "close", [this](const Event &) {
        extras_open_ = false;
        reading_text_.clear();
        rebuild_ = true;
    });
    bindings_.On("extras", "dismiss", [this](const Event &) {
        reading_text_.clear();
        rebuild_ = true;
    });
    bindings_.On("extras-reading-close", "", [this](const Event &) {
        reading_text_.clear();
        rebuild_ = true;
    });
    bindings_.On("extras-note", "", [this, note](const Event &) {
        reading_title_.clear();
        reading_text_ = note;
        rebuild_ = true;
    });
    return view;
}

bool Router::OnEvent(const Event &event)
{
    if (!screen_)
        return false;
    auto found = bindings_.handlers_.find(event.id + '\x1f' + event.verb);
    if (found == bindings_.handlers_.end())
        found = bindings_.handlers_.find(event.id + '\x1f');
    if (found == bindings_.handlers_.end())
    {
        surface_.Error("screen " + using_ + ": no mapping for event " + event.id + " " + event.verb + " " + event.value);
        return false;
    }
    const Bindings::Handler handler = found->second; // a rebuild replaces the table
    current_ = &event;
    sent_in_event_ = false;
    handler(event);
    current_ = nullptr;
    if (sent_in_event_)
    {
        // An action row of the «Прочее» sheet closes it, as a menu does.
        if (event.id.rfind("extra-a", 0) == 0)
            extras_open_ = false;
        pending_.push_back({event.id, event.value, model_.revision, surface_.Now() + kPendingSeconds});
        rebuild_ = true;
    }
    if (rebuild_ && screen_)
        Build();
    return true;
}

void Router::Back()
{
    Action engine_back;
    engine_back.screen = model_.screen;
    engine_back.opening = model_.opening;
    engine_back.verb = "back";
    if (!screen_)
    {
        surface_.Send(engine_back);
        return;
    }
    if (WantsBack())
    {
        screen_->Back(bindings_);
        if (rebuild_ && screen_)
            Build();
        return;
    }
    if (!reading_text_.empty() || extras_open_)
    {
        reading_text_.clear();
        extras_open_ = false;
        Build();
        return;
    }
    if (bindings_.handlers_.count(std::string("header-back") + '\x1f'))
    {
        OnEvent({"header-back", "click", ""});
        return;
    }
    surface_.Send(engine_back);
}

void Router::SubmitText(const std::string &token, const std::string &text, bool accepted)
{
    if (token.empty() || token != edit_token_)
        return;
    edit_token_.clear();
    auto handler = std::move(edit_handler_);
    edit_handler_ = nullptr;
    if (!accepted || !screen_ || edit_action_.screen != model_.screen || edit_action_.opening != model_.opening)
        return;
    if (handler)
    {
        handler(text);
        if (rebuild_ && screen_)
            Build();
        return;
    }
    Action action = edit_action_;
    action.value = text;
    surface_.Send(action);
}

bool Router::WantsBack() const
{
    return screen_ && screen_->OwnsBack();
}


double Router::Tick()
{
    const double now = surface_.Now();
    if (settle_until_ > 0.0)
    {
        if (now < settle_until_)
            return settle_until_ - now;
        Build();
    }
    if (!screen_)
        return kIdle;
    if (rebuild_at_ > 0.0 && now >= rebuild_at_)
        Build();
    double next = rebuild_at_ > 0.0 ? rebuild_at_ - now : kIdle;
    // In the order they were asked for: an edit goes out before the press that confirms it.
    for (size_t i = 0; i < waiting_.size();)
    {
        if (now >= waiting_[i].due)
        {
            last_flush_ = now;
            if (waiting_[i].lead)
                surface_.Send(*waiting_[i].lead);
            surface_.Send(waiting_[i].action);
            waiting_.erase(waiting_.begin() + static_cast<long>(i));
        }
        else
        {
            next = std::min(next, waiting_[i].due - now);
            ++i;
        }
    }
    const size_t before = pending_.size();
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const PendingMark &mark) { return now >= mark.until; }),
                   pending_.end());
    for (const PendingMark &mark : pending_)
        next = std::min(next, mark.until - now);
    if (pending_.size() != before)
        Build();
    return next;
}

} // namespace storm::ui::screens
