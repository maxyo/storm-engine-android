// Dialogue: the "dialogue" state channel becomes an interface model of its own (speaker,
// text, replies, the answer the engine refused, the hero's money), drawn by the pack screen
// <s-dialogue> over the game. The speech is read a page at a time (<ui-pages> in
// C-DialoguePanel); the replies, in two columns, appear with its final page.
//
// Engine commands (the game thread's mailbox, android_dialogue_input.hpp):
//   reply          dialogue:<generation>:<index>
//   typed answer   dialogue:<generation>:<index>:<text>   (an `edit` of the reply)
// A typed answer is refused by the engine with the same generation and
// {editIndex, editError: too_long | invalid_text | unavailable}; the editor then shows why.
//
// A reply the player types opens an editor: a whole-number proposal (a bet: "100") opens the
// number panel (C-Quantity up to the hero's money), anything else the text editor whose
// field opens the system keyboard (IME bridge). The engine's own limits are checked before
// sending, with the engine's code (dialogueEditError).
#include "controls.h"

#include "../hud/hud.h"

#include "../../../util/include/platform/android_dialogue_input.hpp"

#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"SPEAKER", "heading"}, {"TEXT", "text", true}, {"REPLY", "choice"}, {"EDIT_ERROR", "text"}, {"MONEY", "text"},
};

// Message key of an engine refusal; empty for an unknown code.
const char *ErrorKey(const std::string &code)
{
    if (code == "too_long")
        return StringKey("dialogue.too_long");
    if (code == "invalid_text")
        return StringKey("dialogue.invalid");
    if (code == "unavailable")
        return StringKey("dialogue.unavailable");
    return "";
}

bool WholeNumber(const std::string &text)
{
    if (text.empty() || text.size() > 12)
        return false;
    for (const char c : text)
        if (c < '0' || c > '9')
            return false;
    return true;
}

} // namespace

// The checks a typed answer passes before it is sent: the engine's own (UTF-8, one line,
// at most 256 code points / 1024 bytes) and the reply's limit. Empty when it may be sent.
std::string DialogueAnswerError(const std::string &text, int max_code_points)
{
    const std::string_view engine = storm::android::dialogueEditError(text);
    if (!engine.empty())
        return std::string(engine);
    if (max_code_points >= 0 && Length(text) > static_cast<size_t>(max_code_points))
        return "too_long";
    return {};
}

namespace
{

class DialogueScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-dialogue";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    bool OwnsBack() const override
    {
        return editing_ >= 0;
    }
    void Back(Bindings &bindings) override
    {
        Close();
        bindings.Rebuild();
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *refusal = admitted.One("EDIT_ERROR");
        const Widget *money = admitted.One("MONEY");
        const std::vector<const Widget *> replies = admitted.All("REPLY");
        const Widget *editing = nullptr;
        for (const Widget *reply : replies)
            if (reply->index == editing_ && reply->Offers("edit"))
                editing = reply;
        if (editing_ >= 0 && !editing)
            Close(); // the reply is gone: nothing to answer any more
        // The engine refused the answer that was sent: the editor shows why and the draft.
        const std::string refusal_mark = refusal && refusal->context.count("revision") ? refusal->context.at("revision") : std::string();
        if (editing && sent_ && refusal && refusal->index == editing_ && refusal_mark != seen_refusal_)
        {
            seen_refusal_ = refusal_mark;
            sent_ = false;
            error_ = refusal->label;
            if (!*ErrorKey(error_))
                bindings.Toast(bindings.Text("dialogue.unavailable")); // keeps the editor usable
        }

        Json rows = Json::array();
        for (const Widget *reply : replies)
        {
            const std::string id = "reply-" + std::to_string(reply->index);
            const bool editable = reply->Offers("edit");
            std::string title = reply->label;
            if (title.find_first_not_of(" \t") == std::string::npos)
                title = bindings.Text(editable ? "dialogue.input" : "dialogue.continue");
            bindings.On(id, "click", [this, &bindings, reply, editable](const Event &) {
                if (editable)
                    Open(*reply, bindings);
                else
                    bindings.Send(*reply, "reply", reply->row_id);
            });
            rows.push_back(Json{{"id", id}, {"title", title}, {"hold", true}, {"pressed", bindings.Pending(id) || (sent_ && reply == editing)}});
        }
        bindings.On("dialogue-next", "click", [this, &bindings](const Event &) {
            ++page_;
            bindings.Rebuild();
        });
        bindings.On("dialogue-menu", "click", [&bindings](const Event &) { bindings.Platform("pause"); });

        Json view{{"speaker", admitted.Label("SPEAKER")}, {"text", admitted.Label("TEXT")}, {"page", page_}, {"replies", rows},
                  {"edit", nullptr}};
        if (editing)
            view["edit"] = number_ ? NumberEditor(*editing, money, bindings) : TextEditor(*editing, bindings);
        return view;
    }

  private:
    void Open(const Widget &reply, Bindings &bindings)
    {
        editing_ = reply.index;
        sent_ = false;
        error_.clear();
        draft_ = reply.value;
        number_ = WholeNumber(reply.value);
        amount_ = number_ ? std::strtoll(reply.value.c_str(), nullptr, 10) : 0;
        bindings.Rebuild();
    }

    void Close()
    {
        editing_ = -1;
        sent_ = false;
        error_.clear();
    }

    void Submit(const Widget &reply, const std::string &text, Bindings &bindings)
    {
        if (sent_)
            return;
        const auto limit = reply.context.find("maxCodePoints");
        error_ = DialogueAnswerError(text, limit != reply.context.end() ? std::atoi(limit->second.c_str()) : -1);
        if (error_.empty())
        {
            sent_ = true;
            bindings.Send(reply, "edit", text);
        }
        bindings.Rebuild();
    }

    Json Actions(Bindings &bindings, const std::string &sub) const
    {
        Json done{{"id", "edit-done"}, {"label", bindings.Text("dialogue.done")}, {"variant", "primary"}, {"disabled", sent_}};
        if (!sub.empty())
            done["sub"] = sub;
        return Json::array({Json{{"id", "edit-cancel"}, {"label", bindings.Text("common.cancel")}, {"variant", "quiet"}}, done});
    }

    void BindClose(Bindings &bindings)
    {
        bindings.On("edit-cancel", "click", [this, &bindings](const Event &) {
            Close();
            bindings.Rebuild();
        });
        bindings.Ignore("edit", "dismiss"); // a tap beside the editor keeps it (as the old editor did)
    }

    Json TextEditor(const Widget &reply, Bindings &bindings)
    {
        const Widget *target = &reply;
        const auto ask = [this, &bindings, target](const Event &event) {
            if (sent_)
                return;
            bindings.AskText(event, draft_, bindings.Text("dialogue.input"), false, [this, &bindings, target](const std::string &text) {
                draft_ = text;
                Submit(*target, draft_, bindings); // the keyboard's «Done» is the editor's «Done»
            });
        };
        bindings.On("edit-field", "focus", ask);
        bindings.On("edit-field", "click", ask);
        bindings.On("edit-done", "click", [this, &bindings, target](const Event &) { Submit(*target, draft_, bindings); });
        BindClose(bindings);
        const char *key = ErrorKey(error_);
        return Json{{"kind", "text"},
                    {"title", bindings.Text("dialogue.input")},
                    {"value", draft_},
                    {"error", error_.empty() ? std::string() : bindings.Text(*key ? key : "dialogue.unavailable")},
                    {"actions", Actions(bindings, {})}};
    }

    Json NumberEditor(const Widget &reply, const Widget *money, Bindings &bindings)
    {
        const long long have = money ? std::strtoll(money->value.c_str(), nullptr, 10) : -1;
        const long long top = have >= 0 ? have : std::max<long long>(amount_, 0);
        amount_ = std::clamp<long long>(amount_, 0, std::max<long long>(top, 0));
        const Widget *target = &reply;
        const auto set = [this, &bindings](const Event &event) {
            amount_ = std::llround(std::strtod(event.value.c_str(), nullptr));
            bindings.Rebuild();
        };
        bindings.On("edit-amount", "change", set);
        bindings.On("edit-amount", "commit", set);
        bindings.On("edit-done", "click", [this, &bindings, target](const Event &) { Submit(*target, std::to_string(amount_), bindings); });
        BindClose(bindings);
        const char *key = ErrorKey(error_);
        return Json{{"kind", "number"},
                    {"title", bindings.Text("dialogue.amount")},
                    {"money", have},
                    {"value", amount_},
                    {"max", top},
                    {"error", error_.empty() ? std::string() : bindings.Text(*key ? key : "dialogue.unavailable")},
                    {"actions", Actions(bindings, std::to_string(amount_))}};
    }

    int page_ = 0;     // «next» presses on this utterance (a new generation is a new screen)
    int editing_ = -1; // index of the reply being typed, -1 = no editor
    bool number_ = false;
    bool sent_ = false; // the answer went to the engine and has not been answered yet
    std::string draft_, error_;
    long long amount_ = 0;
    std::string seen_refusal_;
};

class DialogueConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "dialogue";
    }
    void OnState(const Json &state, Router &router) override
    {
        if (!state.is_object() || !state.value("active", false))
        {
            if (router.Using() == "dialogue")
                router.CloseInterface();
            return;
        }
        InterfaceModel model;
        model.screen = "dialogue";
        model.opening = state.value("generation", uint64_t{0});
        model.revision = ++revision_;
        // The dialogue speaks the game's language: the one the gameplay state names.
        if (router.Hud())
            model.language = router.Hud()->Current().gameplay.language;
        // The game's texts may carry escaped line breaks ("\n" as two characters).
        const auto text = [](const Json &object, const char *key) {
            const auto found = object.find(key);
            std::string out = found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
            for (size_t at = 0; (at = out.find('\\', at)) != std::string::npos;)
            {
                if (at + 1 < out.size() && out[at + 1] == 'n')
                    out.replace(at, 2, "\n");
                else if (at + 1 < out.size() && out[at + 1] == 'r')
                    out.erase(at, 2);
                else
                    ++at;
            }
            return out;
        };
        Widget speaker;
        speaker.node = "SPEAKER";
        speaker.type = "heading";
        speaker.label = text(state, "speaker");
        model.widgets.push_back(speaker);
        Widget body;
        body.node = "TEXT";
        body.type = "text";
        body.label = text(state, "text");
        model.widgets.push_back(body);
        const auto choices = state.find("choices");
        if (choices != state.end() && choices->is_array())
            for (const Json &choice : *choices)
            {
                if (!choice.is_object() || !choice.contains("index") || !choice["index"].is_number_integer())
                {
                    router.ReportError("dialogue: a choice without an index: " + choice.dump());
                    continue;
                }
                Widget reply;
                reply.node = "REPLY";
                reply.type = "choice";
                reply.index = choice["index"].get<int>();
                reply.label = text(choice, "text");
                reply.value = reply.label; // an editable reply proposes its text as the answer
                reply.row_id = std::to_string(model.opening) + ":" + std::to_string(reply.index);
                const bool editable = choice.value("editable", false);
                reply.actions = {editable ? "edit" : "reply"};
                if (editable)
                    reply.context["maxCodePoints"] = std::to_string(choice.value("maxCodePoints", 256));
                model.widgets.push_back(std::move(reply));
            }
        const auto refused = state.find("editError");
        if (refused != state.end() && refused->is_string())
        {
            Widget error;
            error.node = "EDIT_ERROR";
            error.type = "text";
            error.index = state.value("editIndex", -1);
            error.label = refused->get<std::string>();
            error.context["revision"] = std::to_string(model.revision); // every refusal counts, even a repeated one
            if (!*ErrorKey(error.label))
                router.ReportError("dialogue: unknown answer refusal '" + error.label + "'");
            model.widgets.push_back(std::move(error));
        }
        const auto money = state.find("money");
        if (money != state.end() && money->is_number_integer())
        {
            Widget have;
            have.node = "MONEY";
            have.type = "text";
            have.value = std::to_string(money->get<long long>());
            have.label = have.value;
            model.widgets.push_back(std::move(have));
        }
        for (size_t i = 0; i < model.widgets.size(); ++i)
            model.widgets[i].order = static_cast<int>(i);
        router.SetInterface(model);
    }

  private:
    uint64_t revision_ = 0;
};

} // namespace

std::unique_ptr<Screen> CreateDialogueScreen()
{
    return std::make_unique<DialogueScreen>();
}

std::unique_ptr<StateConsumer> CreateDialogueConsumer()
{
    return std::make_unique<DialogueConsumer>();
}

} // namespace storm::ui::screens
