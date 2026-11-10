// Settings (interface "option_screen", mockups V2-Settings-Sound, -Controls, -Graphics,
// -Image, -Interface): four pages on a rail, each in two columns of C-Setting rows. The
// game applies its own controls as they change and keeps or drops them with BTN_OK /
// BTN_CANCEL: «Готово» is BTN_OK, ← is BTN_CANCEL, and there is no unsaved-changes question
// (owner decision). The renderer options of the application (texture filtering and
// quality, vertical sync, character shadows, sea foam) sit on the image page; they come
// from the platform host on the state channel "appSettings", are kept as a draft and go
// back to the host with «Готово» (Bindings::SendHost). They take effect after a restart.
// Keyboard bindings are not offered at all (mobile only); colour correction is not
// available in the Android renderer. Fills the pack screen <s-settings>.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"BTN_OK", "button", true},
    {"BTN_CANCEL", "button", true},
    {"MUSIC_SLIDE", "slider"},
    {"SOUND_SLIDE", "slider"},
    {"DIALOG_SLIDE", "slider"},
    {"VMOUSE_SENSITIVITY_SLIDE", "slider"},
    {"HMOUSE_SENSITIVITY_SLIDE", "slider"},
    {"SEACAMPERSP_SLIDE", "slider"},
    {"GLOW_SLIDE", "slider"},
    {"SEA_DETAILS_SLIDE", "slider"},
    {"HUD_SLIDE", "slider"},
    {"AUTOSAVE_CHECKBOX", "check"},
    {"DIRECTSAIL_CHECKBOX", "check"},
    {"ENHANCEDSAILING_CHECKBOX", "check"},
    {"INVERT_MOUSE_CHECKBOX", "check"},
    {"ALWAYS_RUN_CHECKBOX", "check"},
    {"CAMERASWING_CHECKBOX", "check"},
    {"ROTATESKY_CHECKBOX", "check"},
    {"DYNAMICLIGHTS_CHECKBOX", "check"},
    {"HERB_CHECKBOX", "check"},
    {"SIMPLESEA_CHECKBOX", "check"},
    {"CREWONDECK_CHECKBOX", "check"},
    {"QUESTMARK_CHECKBOX", "check"},
    {"SHIPMARK_CHECKBOX", "check"},
    {"BATTLE_MODE_CHECKBOX", "check"},
    {"HUD_DESCRIP_TEXT", "text"},
    // Not offered: colour correction (unavailable in the Android renderer), keyboard
    // bindings (mobile only), the game's own captions and pictures of its layout.
    {"GAMMA_SLIDE", "slider"},
    {"BRIGHT_SLIDE", "slider"},
    {"CONTRAST_SLIDE", "slider"},
    {"CONTROLS_LIST", "choice"},
    {"CHANGEKEY_*", "*"},
    {"KEY_CHOOSER", "*"},
    {"TABBTN_*", "image|button"},
    {"TABSTR_*", "text"},
    {"TITLE", "heading|text"},
    {"TITLES_STR", "text"},
    {"TITLES_IMAGES", "image"},
    {"HARDWARE_SETTINGS", "text"},
    {"SOFTWARE_SETTINGS", "text"},
    {"MAIN_BACK", "image"},
};

const char *const kTabs[] = {StringKey("settings.tab_sound"), StringKey("settings.tab_controls"), StringKey("settings.tab_image"),
                             StringKey("settings.tab_interface")};

// The application's renderer options as the host last reported them.
Json &AppSettings()
{
    static Json settings;
    return settings;
}

class AppSettingsConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "appSettings";
    }
    void OnState(const Json &state, Router &) override
    {
        AppSettings() = state;
    }
};

struct Column
{
    std::string title;
    Json rows = Json::array();
};

class SettingsScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-settings";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        if (!draft_.is_object())
            draft_ = AppSettings();

        // One game control (or the radio group of a node) as a row; nothing when absent.
        const auto game = [&admitted, &bindings](Column &column, const char *node, const char *label = nullptr) {
            const std::vector<const Widget *> widgets = admitted.All(node);
            if (widgets.empty())
                return;
            const std::vector<std::string> labels = label ? std::vector<std::string>{bindings.Text(label)} : std::vector<std::string>();
            ControlRows(bindings, widgets, std::string(node) + "-", labels, column.rows);
        };

        std::vector<std::pair<std::string, std::vector<Column>>> sections;
        if (tab_ == 0)
        {
            Column volume{bindings.Text("settings.volume")}, play{bindings.Text("settings.game")};
            game(volume, "MUSIC_SLIDE", "settings.music");
            game(volume, "SOUND_SLIDE", "settings.sound_effects");
            game(volume, "DIALOG_SLIDE", "settings.voices");
            game(play, "AUTOSAVE_CHECKBOX");
            game(play, "DIRECTSAIL_CHECKBOX");
            game(play, "ENHANCEDSAILING_CHECKBOX");
            sections.push_back({"", {volume, play}});
        }
        else if (tab_ == 1)
        {
            Column camera{bindings.Text("settings.camera")}, movement{bindings.Text("settings.movement")};
            game(camera, "VMOUSE_SENSITIVITY_SLIDE", "settings.vertical");
            game(camera, "HMOUSE_SENSITIVITY_SLIDE", "settings.horizontal");
            game(camera, "SEACAMPERSP_SLIDE", "settings.sea_camera");
            game(camera, "INVERT_MOUSE_CHECKBOX");
            game(movement, "ALWAYS_RUN_CHECKBOX");
            game(movement, "CAMERASWING_CHECKBOX");
            sections.push_back({"", {camera, movement}});
        }
        else if (tab_ == 2)
        {
            Column left, right;
            Segment(bindings, left, "texture_anisotropy", "settings.filtering", {1, 2, 4, 8, 16},
                    {bindings.Text("settings.filter_basic"), "2×", "4×", "8×", "16×"});
            Toggle(bindings, left, "vsync", "settings.vsync");
            Segment(bindings, right, "texture_degradation", "settings.texture_quality", {0, 1, 2},
                    {bindings.Text("settings.texture_full"), bindings.Text("settings.texture_medium"), bindings.Text("settings.texture_low")});
            Toggle(bindings, right, "character_shadows", "settings.shadows");
            sections.push_back({bindings.Text("settings.graphics"), {left, right}});
            Column effects{bindings.Text("settings.effects")}, sea{bindings.Text("settings.sea")};
            game(effects, "GLOW_SLIDE", "settings.glow");
            game(effects, "ROTATESKY_CHECKBOX");
            game(effects, "DYNAMICLIGHTS_CHECKBOX");
            game(effects, "HERB_CHECKBOX", "settings.grass");
            game(sea, "SEA_DETAILS_SLIDE", "settings.sea_detail");
            game(sea, "SIMPLESEA_CHECKBOX");
            game(sea, "CREWONDECK_CHECKBOX");
            Toggle(bindings, sea, "sea_foam", "settings.sea_foam");
            sections.push_back({"", {effects, sea}});
        }
        else
        {
            Column hud{bindings.Text("settings.hud")}, markers{bindings.Text("settings.markers")};
            game(hud, "HUD_SLIDE", "settings.hud_scale");
            // The game shows the scale it derives from the slider: "1080 / 900 : 1.2".
            const std::string scale = admitted.Label("HUD_DESCRIP_TEXT");
            if (const size_t colon = scale.rfind(':'); colon != std::string::npos && !hud.rows.empty())
            {
                std::string factor = scale.substr(colon + 1);
                factor.erase(0, factor.find_first_not_of(' '));
                hud.rows.back()["display"] = "×" + factor;
            }
            game(markers, "QUESTMARK_CHECKBOX");
            game(markers, "SHIPMARK_CHECKBOX");
            game(markers, "BATTLE_MODE_CHECKBOX");
            sections.push_back({"", {hud, markers}});
        }

        Json view = Json::object();
        view["title"] = bindings.Text("settings.title");
        Json tabs = Json::array();
        for (size_t i = 0; i < std::size(kTabs); ++i)
            tabs.push_back(Json{{"label", bindings.Text(kTabs[i])}, {"selected", static_cast<int>(i) == tab_}});
        view["tabs"] = tabs;
        bindings.On("tabs", "select", [this, &bindings](const Event &event) {
            tab_ = std::clamp(std::atoi(event.value.c_str()), 0, static_cast<int>(std::size(kTabs)) - 1);
            bindings.Rebuild();
        });
        Json pages = Json::array();
        for (const auto &[title, columns] : sections)
        {
            Json cols = Json::array();
            for (const Column &column : columns)
                cols.push_back(Json{{"title", column.title}, {"rows", column.rows}});
            pages.push_back(Json{{"title", title}, {"columns", cols}});
        }
        view["sections"] = pages;

        const Widget *ok = admitted.One("BTN_OK");
        const Widget *cancel = admitted.One("BTN_CANCEL");
        view["done"] = Json{{"label", bindings.Text("settings.done")},
                            {"disabled", !ok->Offers("press")},
                            {"reason", ok->unavailable_reason},
                            {"busy", bindings.Pending("done")}};
        bindings.On("done", "", [this, &bindings, ok](const Event &) {
            if (draft_.is_object() && draft_ != AppSettings())
            {
                bindings.SendHost("app-settings", draft_.dump());
                AppSettings() = draft_;
            }
            bindings.Send(*ok, "press");
        });
        bindings.On("header-back", "", [&bindings, cancel](const Event &) { bindings.Back(cancel); });
        return view;
    }

  private:
    // A renderer option the host reported; without the report the row stays visible but
    // disabled, and a tap says why.
    bool Known(const char *key) const
    {
        return draft_.is_object() && draft_.contains(key) && draft_[key].is_number_integer();
    }

    void Segment(Bindings &bindings, Column &column, const char *key, const char *label, std::vector<int> values, Json options)
    {
        int selected = 0;
        for (size_t i = 0; i < values.size(); ++i)
            if (Known(key) && draft_[key].get<int>() == values[i])
                selected = static_cast<int>(i);
        const std::string id = std::string("app-") + key;
        column.rows.push_back(Json{{"kind", "segment"},
                                   {"id", id},
                                   {"label", bindings.Text(label)},
                                   {"options", options},
                                   {"selected", selected},
                                   {"stack", true},
                                   {"restart", true},
                                   {"disabled", !Known(key)},
                                   {"reason", Known(key) ? std::string() : bindings.Text("settings.app_unavailable")}});
        bindings.On(id, "change", [this, &bindings, key, values](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < values.size() && Known(key))
                draft_[key] = values[index];
            bindings.Rebuild();
        });
    }

    void Toggle(Bindings &bindings, Column &column, const char *key, const char *label)
    {
        const std::string id = std::string("app-") + key;
        column.rows.push_back(Json{{"kind", "toggle"},
                                   {"id", id},
                                   {"label", bindings.Text(label)},
                                   {"on", Known(key) && draft_[key].get<int>() != 0},
                                   {"restart", true},
                                   {"disabled", !Known(key)},
                                   {"reason", Known(key) ? std::string() : bindings.Text("settings.app_unavailable")}});
        bindings.On(id, "change", [this, &bindings, key](const Event &event) {
            if (Known(key))
                draft_[key] = event.value == "1" ? 1 : 0;
            bindings.Rebuild();
        });
    }

    int tab_ = 0;
    Json draft_; // the renderer options as the player set them on this opening
};

} // namespace

std::unique_ptr<Screen> CreateSettingsScreen()
{
    return std::make_unique<SettingsScreen>();
}

std::unique_ptr<StateConsumer> CreateAppSettingsConsumer()
{
    return std::make_unique<AppSettingsConsumer>();
}

} // namespace storm::ui::screens
