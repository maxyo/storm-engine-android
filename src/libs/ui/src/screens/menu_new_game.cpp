// New game (interface "selectcharacter", mockup V2-Menu-NewGame): the game options on the
// left (difficulty, sea battle type, the option toggles), the hero, the occupation and
// «Начать игру» on the right. The game's own windows over this interface (CONFIRM_WINDOW_*,
// NOTE_WINDOW_*) become a C-Dialog with the game's buttons. Profiles are not offered
// (owner decision). Fills the pack screen <s-new-game>.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"OK_BUTTON", "button", true},
    {"CANCEL_BUTTON", "button", true},
    {"TITLE", "heading|text"},
    {"HERO_NAME", "text"},
    {"LEFTCHANGE_CHARACTER", "button"},
    {"RIGHTCHANGE_CHARACTER", "button"},
    {"FACEPICT", "image"},
    {"NATIONS_PICTURE", "image"},
    {"INFO_FRAME_CAPTION", "text"},
    {"INFO_TEXT", "text"},
    {"STATE_FRAME_CAPTION", "text"},
    {"COMPLEX_TYPE", "text"},
    {"LEFTCHANGE_COMPLEX", "button"},
    {"RIGHTCHANGE_COMPLEX", "button"},
    {"SETUP_FRAME_CAPTION", "text"},
    {"ANDROID_HERO_TYPE", "text"}, // the occupation's key (HeroType_N), engine
    {"SETUP_TEXT", "text"},
    {"LEFTCHANGE_SETUP", "button"},
    {"RIGHTCHANGE_SETUP", "button"},
    {"TABLE_SMALLSKILL", "choice"}, // the starting attributes: not on the mockup (V2-Menu-NewGame)
    {"CHECK_ARCADESAIL_NAME", "text"},
    {"CHECK_ARCADESAIL", "check"},
    {"CHECK_OTHER_NAME", "text"},
    {"ANDROID_TUTORIAL_SKIP", "check"},
    {"CHECK_HARDCORE", "check"},
    {"CHECK_PISTOL", "check"},
    {"CHECK_PARTITION", "check"},
    {"CHECK_SEABATTLESAVE", "check"},
    {"CHECK_RAINS", "check"},
    {"CONFIRM_WINDOW_*", "text|button|image"},
    {"NOTE_WINDOW_*", "text|button|image"},
    // Not offered: the profile (no profiles in this port) and the layout's background.
    {"PROFILE_*", "*"},
    {"MAIN_BACK", "image"},
    {"EXIT_BTN", "button"},
};

std::string Trimmed(std::string text)
{
    text.erase(0, text.find_first_not_of(" \t"));
    text.erase(text.find_last_not_of(" \t") + 1);
    return text;
}

// Our nation icon and name key for the game's NATIONS picture.
std::pair<std::string, const char *> Nation(const ImageRef *picture)
{
    static const char *const nations[][3] = {{"FRANCE", "nation-france", StringKey("menu.nation_france")},
                                             {"ENGLAND", "nation-england", StringKey("menu.nation_england")},
                                             {"SPAIN", "nation-spain", StringKey("menu.nation_spain")},
                                             {"HOLLAND", "nation-holland", StringKey("menu.nation_holland")},
                                             {"PIRATE", "nation-pirates", StringKey("menu.nation_pirates")}};
    const std::string name = picture ? Upper(picture->image) : std::string();
    for (const auto &nation : nations)
        if (name.rfind(nation[0], 0) == 0)
            return {nation[1], nation[2]};
    return {"", nullptr};
}

class NewGameScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-new-game";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        // A selector: the game's caption between its ‹ › buttons.
        const auto stepper = [&admitted, &bindings](const char *id, const std::string &label, const char *text, const char *left,
                                                     const char *right) -> Json {
            const Widget *value = admitted.One(text);
            if (!value)
                return Json();
            const Widget *prev = admitted.One(left), *next = admitted.One(right);
            if (prev)
                bindings.Forward(std::string(id) + "-left", *prev, "press");
            if (next)
                bindings.Forward(std::string(id) + "-right", *next, "press");
            return Json{{"kind", "stepper"},
                        {"id", id},
                        {"label", label},
                        {"text", Trimmed(value->label)},
                        {"left", prev && prev->Offers("press")},
                        {"right", next && next->Offers("press")}};
        };

        Json rows = Json::array();
        if (Json difficulty = stepper("difficulty", bindings.Text("menu.difficulty"), "COMPLEX_TYPE", "LEFTCHANGE_COMPLEX", "RIGHTCHANGE_COMPLEX");
            !difficulty.is_null())
            rows.push_back(difficulty);
        // Sea battle type: the game's radio pair as one segment, named by its caption.
        const std::vector<const Widget *> battle = admitted.All("CHECK_ARCADESAIL");
        if (!battle.empty())
        {
            Json options = Json::array();
            int selected = 0;
            bool usable = false;
            for (size_t i = 0; i < battle.size(); ++i)
            {
                options.push_back(battle[i]->label);
                if (battle[i]->value == "1")
                    selected = static_cast<int>(i);
                usable = usable || battle[i]->Offers("toggle");
            }
            bindings.On("battle", "change", [&bindings, battle](const Event &event) {
                const size_t chosen = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (chosen < battle.size() && battle[chosen]->Offers("toggle"))
                    bindings.Send(*battle[chosen], "toggle", "1");
            });
            rows.push_back(Json{{"kind", "segment"},
                                {"id", "battle"},
                                {"label", Trimmed(admitted.Label("CHECK_ARCADESAIL_NAME"))},
                                {"options", options},
                                {"selected", selected},
                                {"disabled", !usable}});
        }

        Json toggles = Json::array();
        // The port's own option: start in Saint-Pierre without the tutorial. The engine binds
        // the value to the row identity it published.
        if (const Widget *skip = admitted.One("ANDROID_TUTORIAL_SKIP"))
        {
            bindings.On("tutorial", "change", [&bindings, skip](const Event &event) {
                bindings.Send(*skip, "toggle", (event.value == "1" ? "1@" : "0@") + skip->row_id);
            });
            toggles.push_back(Json{{"kind", "toggle"},
                                   {"id", "tutorial"},
                                   {"label", skip->label},
                                   {"on", skip->value == "1"},
                                   {"disabled", !skip->Offers("toggle")},
                                   {"reason", skip->unavailable_reason}});
        }
        for (const char *node : {"CHECK_HARDCORE", "CHECK_PISTOL", "CHECK_PARTITION", "CHECK_SEABATTLESAVE", "CHECK_RAINS"})
        {
            const std::vector<const Widget *> checks = admitted.All(node);
            ControlRows(bindings, checks, std::string(node) + "-", {}, toggles);
        }
        // The game's interface file labels the hardcore toggle with its untranslated text key
        // ("HardcoreGame" in every language): our string names it.
        for (Json &toggle : toggles)
            if (toggle.value("label", std::string()) == "HardcoreGame")
                toggle["label"] = bindings.Text(StringKey("menu.hardcore"));

        Json view = Json::object();
        view["title"] = bindings.Text("menu.new_game_title");
        view["rows"] = rows;
        view["toggles"] = toggles;

        const Widget *face = admitted.One("FACEPICT");
        const ImageRef *flag = admitted.One("NATIONS_PICTURE") ? admitted.One("NATIONS_PICTURE")->FirstImage() : nullptr;
        const auto [icon, nation] = Nation(flag);
        const std::string bio = admitted.Label("INFO_TEXT");
        const std::string bio_title = Trimmed(admitted.Label("INFO_FRAME_CAPTION"));
        Json portrait = face ? PictureOf(face->FirstImage()) : Json();
        if (portrait.is_null())
        {
            // The hero's portrait is a game texture the engine names by face; without one the
            // neutral portrait stands in, and the log says so.
            if (face && reported_portrait_ != admitted.model->opening)
            {
                reported_portrait_ = admitted.model->opening;
                bindings.Error("new game: the hero picture (FACEPICT) names no portrait face; neutral portrait shown");
            }
            portrait = Json{{"kind", "portrait"}};
        }
        view["hero"] = Json{{"name", Trimmed(admitted.Label("HERO_NAME"))},
                            {"nation", nation ? bindings.Text(nation) : std::string()},
                            {"image", portrait},
                            {"bio", !bio.empty()}};
        bindings.On("bio", "", [&bindings, bio, bio_title](const Event &) { bindings.Read(bio_title, bio); });
        Json setup = stepper("setup", "", "SETUP_FRAME_CAPTION", "LEFTCHANGE_SETUP", "RIGHTCHANGE_SETUP");
        // The game captions the occupation with a fragment of a sentence in the accusative
        // ("гимнастику и искусство фехтования", common.ini HeroType_N, Russian only): the
        // stock occupations get their title from our tables; another (a mod's) keeps the
        // game's caption, and the log says so.
        static const char *const kOccupations[][2] = {{"HeroType_1", StringKey("menu.hero_type_1")},
                                                      {"HeroType_2", StringKey("menu.hero_type_2")},
                                                      {"HeroType_3", StringKey("menu.hero_type_3")},
                                                      {"HeroType_4", StringKey("menu.hero_type_4")}};
        if (const Widget *type = admitted.One("ANDROID_HERO_TYPE"); type && !setup.is_null())
        {
            bool known = false;
            for (const auto &[key, title] : kOccupations)
                if (type->label == key)
                    setup["text"] = bindings.Text(title), known = true;
            if (!known && reported_occupation_ != type->label)
            {
                reported_occupation_ = type->label;
                bindings.Error("new game: occupation '" + type->label + "' has no title of ours; the game's caption is shown");
            }
        }
        const std::string setup_text = admitted.Label("SETUP_TEXT");
        if (!setup.is_null())
            setup["note"] = setup_text;
        view["setup"] = setup;


        const Widget *ok = admitted.One("OK_BUTTON");
        view["start"] = Json{{"label", bindings.Text("menu.start")},
                             {"disabled", !ok->Offers("press")},
                             {"reason", ok->unavailable_reason},
                             {"busy", bindings.Pending("start")}};
        bindings.Forward("start", *ok, "press");
        const Widget *cancel = admitted.One("CANCEL_BUTTON");
        bindings.On("header-back", "", [&bindings, cancel](const Event &) { bindings.Back(cancel); });

        // «Начать игру» with the game's default profile name asks whether to use the existing
        // profile folder (selectCharacter.c exitOk → ShowConfirmWindow; «да» keeps its saves and
        // starts). There are no profiles in this port: the question is answered for the player.
        const Widget *profile_yes = admitted.One("CONFIRM_WINDOW_MB_YES");
        if (profile_yes && profile_yes->Offers("press") && answered_ != admitted.model->revision)
        {
            answered_ = admitted.model->revision;
            bindings.Send(*profile_yes, "press");
            view["modal"] = Json();
        }
        else
            view["modal"] = Modal(admitted, bindings);
        return view;
    }

  private:
    // The game's window over the interface: its texts and its buttons.
    static Json Modal(const Admission &admitted, Bindings &bindings)
    {
        std::string text;
        Json actions = Json::array();
        for (const Widget *widget : admitted.known)
        {
            const std::string node = Upper(widget->node);
            if (node.rfind("CONFIRM_WINDOW_", 0) != 0 && node.rfind("NOTE_WINDOW_", 0) != 0)
                continue;
            if (widget->type == "text" && !Trimmed(widget->label).empty())
                text += (text.empty() ? "" : "\n") + Trimmed(widget->label);
            else if (widget->type == "button" && !widget->label.empty())
            {
                const std::string id = "modal-" + std::to_string(actions.size());
                bindings.Forward(id, *widget, "press");
                actions.push_back(Json{{"id", id},
                                       {"label", widget->label},
                                       {"variant", actions.empty() ? "primary" : "secondary"},
                                       {"disabled", !widget->Offers("press")}});
            }
        }
        if (text.empty() && actions.empty())
            return Json();
        bindings.Ignore("modal", "dismiss"); // the game's window closes only by its buttons
        return Json{{"title", text}, {"text", ""}, {"actions", actions}};
    }

    uint64_t reported_portrait_ = ~0ull; // opening whose missing portrait was reported
    std::string reported_occupation_;   // occupation without a title, reported once
    uint64_t answered_ = 0;             // model revision whose profile question was answered
};

} // namespace

std::unique_ptr<Screen> CreateNewGameScreen()
{
    return std::make_unique<NewGameScreen>();
}

} // namespace storm::ui::screens
