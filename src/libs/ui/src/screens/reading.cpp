// Reading cards: the game's small decision windows over the running game, drawn as one
// parchment C-Dialog (mockups V2-Reading-MapEncounter, -BattleAhead, -Wreck, -DirSail,
// -LeaveBattle, -Salary), and the text interlude `boal_frame` (V2-Reading-Frame): one line on
// a dark scene over the interlude's picture, a tap anywhere is the game's own way out. Fills the pack screen <s-reading>.
//
// The card interfaces share one layout in the game (map.ini, DirSailEnc.ini, LeaveBattle.ini,
// salary.ini): a caption, an info picture, one or more info texts and B_OK / B_CANCEL.
// The picture is the game's painting of the watch; the card shows the icon of the situation.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kCardNodes[] = {
    {"B_OK", "button", true},
    {"B_CANCEL", "button"},
    {"MAP_CAPTION", "text|heading"},
    {"MAIN_WINDOW_CAPTION", "text|heading"},
    {"INFO_TEXT*", "text"},
    {"INFO_PICTURE", "image"},
    {"INFO_BORDERS", "image"},
    {"STATICIMAGES", "image"},
};

constexpr NodeRule kFrameNodes[] = {
    {"INFO_TEXT", "text", true},
    {"SCROLLPICT", "image"},
    {"BACKSCROLLPICT", "image"},
    {"BORDERS", "image"},
};

// The game splits a card's text over several nodes (the news, then the question); the card
// reads them as one paragraph. The interlude keeps its own line breaks.
std::string Joined(const std::vector<const Widget *> &lines, const char *separator)
{
    std::string text;
    for (const Widget *line : lines)
        if (!line->label.empty())
            text += (text.empty() ? "" : separator) + line->label;
    return text;
}

// How each card interface presents its two buttons.
struct CardKind
{
    const char *screen;
    const char *icon;
    const char *cancel_variant; // B_CANCEL, offered or not
    bool salary;                // the amount (INFO_TEXT3) goes on the primary
};

constexpr CardKind kKinds[] = {
    {"map", "spyglass", "quiet", false},          // watch signal on the world map: sail on or engage
    {"dirsailenc", "spyglass", "quiet", false},   // the same signal while sailing straight to an island
    {"leavebattle", "swords", "danger", false},   // leaving companions in a battle: the refusal is the wax seal
    {"salary", "money", "danger", true},           // crew wages: refusing costs morale
};

class CardScreen final : public Screen
{
  public:
    explicit CardScreen(const CardKind &kind) : kind_(kind)
    {
    }
    const char *Component() const override
    {
        return "s-reading";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kCardNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        Json view = Json::object();
        view["mode"] = "card";
        view["icon"] = kind_.icon;
        std::string title = admitted.Label("MAP_CAPTION");
        if (title.empty())
            title = admitted.Label("MAIN_WINDOW_CAPTION");
        view["title"] = title;

        std::vector<const Widget *> lines;
        const Widget *amount = nullptr;
        for (const Widget *widget : admitted.known)
        {
            const std::string node = Upper(widget->node);
            if (node.rfind("INFO_TEXT", 0) != 0 || widget->label.empty())
                continue;
            if (kind_.salary && node == "INFO_TEXT3")
                amount = widget; // the sum to pay: shown on the primary, not in the text
            else
                lines.push_back(widget);
        }
        view["text"] = Joined(lines, " ");

        Json actions = Json::array();
        const Widget *ok = admitted.One("B_OK");
        const Widget *cancel = admitted.One("B_CANCEL");
        if (cancel && !cancel->label.empty())
        {
            const bool offered = cancel->Offers("press");
            // The same kind of control whether the game allows it or not; only its state differs
            // (a disabled quiet «Плыть дальше» is dimmer ink, not a box that reads as a button).
            Json button = ButtonView(bindings, *cancel, "cancel", kind_.cancel_variant);
            if (kind_.salary)
                button["label"] = bindings.Text("reading.salary_refuse");
            if (!offered && cancel->unavailable_reason.empty())
                button["reason"] = bindings.Text(kind_.salary ? "reading.no_choice" : "reading.cannot_leave");
            actions.push_back(button);
        }
        Json primary = ButtonView(bindings, *ok, "ok", "primary");
        if (kind_.salary)
        {
            primary["label"] = bindings.Text("reading.salary_pay");
            if (amount)
                primary["sub"] = "−" + amount->label;
            if (!ok->Offers("press") && ok->unavailable_reason.empty())
                primary["reason"] = bindings.Text("reading.no_money");
        }
        actions.push_back(primary);
        view["actions"] = actions;
        // A forced decision: a tap beside the card answers nothing.
        bindings.Ignore("card", "dismiss");
        return view;
    }

  private:
    const CardKind &kind_;
};

class FrameScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-reading";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kFrameNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        // The interlude ends by itself (the game's timer); a tap ends it early the way the
        // game's Esc does. While the game locks Esc the global back is ignored there too.
        bindings.On("frame", "", [&bindings](const Event &) { bindings.Back(nullptr); });
        // The picture of the moment (a quest's FramePic: boal_frame.c sets it on SCROLLPICT)
        // stands behind the line. Without one SCROLLPICT keeps boal_frame.ini's own texture, a
        // frame ornament the design leaves out (V2-Reading-Frame).
        const Widget *picture = admitted.One("SCROLLPICT");
        Json shown = TextureOf(picture ? picture->FirstImage() : nullptr);
        if (shown.is_object() && shown["texture"] == "interfaces/frameform")
            shown = Json();
        return Json{{"mode", "frame"},
                    {"text", Joined(admitted.All("INFO_TEXT"), "\n")},
                    {"picture", shown},
                    {"title", ""},
                    {"icon", ""},
                    {"actions", Json::array()}};
    }
};

// News of the colonies and of the nations (interfaces "colonyinfo", "nationlegend"): a
// parchment notice with the nations' flags; the game's only answer is its OK, which the close
// cross and a tap beside the notice press as well.
constexpr NodeRule kNoticeNodes[] = {
    {"B_OK", "button", true},
    {"INFO_TEXT", "text"},
    {"INFO_TEXT_QUESTION", "text"},
    {"INFO_CAPTION", "text|heading"},
    {"Nat1", "image"},
    {"Nat2", "image"},
    {"RelNat", "image"},
};

std::string FlagOf(const Widget *picture)
{
    const std::string nation = NationOf(picture ? picture->FirstImage() : nullptr);
    return nation.empty() ? nation : "nation-" + nation;
}

class NoticeScreen final : public Screen
{
  public:
    explicit NoticeScreen(const char *title_key) : title_key_(title_key)
    {
    }
    const char *Component() const override
    {
        return "s-reading";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNoticeNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *ok = admitted.One("B_OK");
        std::string title = admitted.Label("INFO_CAPTION");
        Json flags = Json::array();
        for (const char *node : {"Nat1", "Nat2"})
            if (const std::string flag = FlagOf(admitted.One(node)); !flag.empty())
                flags.push_back(Json{{"icon", flag}});
        if (flags.size() == 2)
        {
            // The relation between them, from the game's relations picture.
            const ImageRef *relation = admitted.One("RelNat") ? admitted.One("RelNat")->FirstImage() : nullptr;
            const std::string name = relation ? relation->image : std::string();
            const char *icon = name == "Enemy" ? "rel-war" : name == "Friend" ? "rel-ally" : "rel-neutral";
            const char *key = name == "Enemy" ? StringKey("reading.war") : name == "Friend" ? StringKey("reading.alliance") : StringKey("reading.peace");
            flags.insert(flags.begin() + 1, Json{{"icon", icon}, {"word", bindings.Text(key)}, {"tone", name == "Enemy" ? "war" : ""}});
        }
        std::vector<const Widget *> lines = admitted.All("INFO_TEXT");
        for (const Widget *question : admitted.All("INFO_TEXT_QUESTION"))
            lines.push_back(question);
        Json view{{"mode", "notice"},
                  {"title", title.empty() ? bindings.Text(title_key_) : title},
                  {"text", Joined(lines, " ")},
                  {"flags", flags},
                  {"icon", ""}};
        Json actions = Json::array();
        // A notice with several flags is a short news line: the cross is enough.
        if (flags.size() < 2)
            actions.push_back(ButtonView(bindings, *ok, "ok", "secondary"));
        else
            bindings.Forward("ok", *ok, "press");
        view["actions"] = actions;
        bindings.Forward("card-close", *ok, "press");
        bindings.Forward("card", *ok, "press");
        return view;
    }

  private:
    const char *title_key_;
};

// Fort capture (interface "fortcapture"): the game opens it and leaves it in the same frame
// (fortcapture.c: straight to the port); the capture itself reaches the player as a log toast
// of the sea HUD (V2-Extra-FortCapture). Nothing is drawn for that moment.
constexpr NodeRule kFortNodes[] = {
    {"SCROLLPICT", "image"},
    {"EXIT_BTN", "button"},
};

class FortCaptureScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-reading";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kFortNodes;
    }
    Json Build(const Admission &, Bindings &) override
    {
        return Json{{"mode", "none"}, {"title", ""}, {"text", ""}, {"icon", ""}, {"actions", Json::array()}};
    }
};

const CardKind &Kind(const char *screen)
{
    for (const CardKind &kind : kKinds)
        if (std::string_view(kind.screen) == screen)
            return kind;
    return kKinds[0];
}

} // namespace

std::unique_ptr<Screen> CreateMapEncounterScreen()
{
    return std::make_unique<CardScreen>(Kind("map"));
}
std::unique_ptr<Screen> CreateDirSailScreen()
{
    return std::make_unique<CardScreen>(Kind("dirsailenc"));
}
std::unique_ptr<Screen> CreateLeaveBattleScreen()
{
    return std::make_unique<CardScreen>(Kind("leavebattle"));
}
std::unique_ptr<Screen> CreateSalaryScreen()
{
    return std::make_unique<CardScreen>(Kind("salary"));
}
std::unique_ptr<Screen> CreateColonyNewsScreen()
{
    return std::make_unique<NoticeScreen>(StringKey("reading.colony_news"));
}
std::unique_ptr<Screen> CreateNationNewsScreen()
{
    return std::make_unique<NoticeScreen>(StringKey("reading.nation_news"));
}
std::unique_ptr<Screen> CreateFortCaptureScreen()
{
    return std::make_unique<FortCaptureScreen>();
}
std::unique_ptr<Screen> CreateFrameScreen()
{
    return std::make_unique<FrameScreen>();
}

} // namespace storm::ui::screens
