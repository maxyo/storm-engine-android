// Dialogue: the "dialogue" state channel becomes an interface model of its own (speaker,
// text, replies), drawn by the pack screen <s-dialogue>. A reply goes back as
// Action{screen "dialogue", verb "reply", value "<generation>:<index>"}.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {{"SPEAKER", "heading"}, {"TEXT", "text", true}, {"REPLY", "choice"}};

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
    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        Json replies = Json::array();
        for (const Widget *reply : admitted.All("REPLY"))
        {
            const std::string id = "reply-" + std::to_string(reply->index);
            const bool editable = reply->Offers("edit");
            bindings.On(id, "", [&bindings, reply, editable](const Event &event) {
                if (editable)
                    bindings.EditText(event, *reply, reply->label, false);
                else
                    bindings.Send(*reply, "reply", reply->row_id);
            });
            replies.push_back(Json{{"id", id}, {"title", reply->label}, {"busy", bindings.Pending(id)}});
        }
        return Json{{"speaker", admitted.Label("SPEAKER")}, {"text", admitted.Label("TEXT")}, {"replies", replies}};
    }
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
        const auto text = [&state](const char *key) {
            const auto found = state.find(key);
            return found != state.end() && found->is_string() ? found->get<std::string>() : std::string();
        };
        Widget speaker;
        speaker.node = "SPEAKER";
        speaker.type = "heading";
        speaker.label = text("speaker");
        model.widgets.push_back(speaker);
        Widget body;
        body.node = "TEXT";
        body.type = "text";
        body.label = text("text");
        model.widgets.push_back(body);
        const auto choices = state.find("choices");
        if (choices != state.end() && choices->is_array())
            for (const Json &choice : *choices)
            {
                if (!choice.is_object())
                    continue;
                Widget reply;
                reply.node = "REPLY";
                reply.type = "choice";
                reply.index = choice.value("index", -1);
                const auto label = choice.find("text");
                reply.label = label != choice.end() && label->is_string() ? label->get<std::string>() : std::string();
                reply.row_id = std::to_string(model.opening) + ":" + std::to_string(reply.index);
                reply.actions = {choice.value("editable", false) ? "edit" : "reply"};
                model.widgets.push_back(std::move(reply));
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
