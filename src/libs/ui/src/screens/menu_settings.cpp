// Settings (interface "option_screen", mockups V2-Settings-Sound, -Controls, -Graphics,
// -Image, -Interface): four pages on a rail, each in two columns of C-Setting rows. The
// game applies its own controls as they change and keeps or drops them with BTN_OK /
// BTN_CANCEL: «Готово» is BTN_OK, ← is BTN_CANCEL, and there is no unsaved-changes question
// (owner decision). The image page is one graphics choice (owner decision): the quality
// preset «Низкая / Средняя / Высокая» (engine renderer_bgfx fx_preset 0/1/2, applied at once:
// the host asks the engine to re-read it; «Низкая» chosen by the automatic preset at a lower scene height shows that
// height under it), plus the texture quality (memory on small phones,
// at the next start), both application options from the platform host on the state channel
// "appSettings", kept as a draft and sent back with «Готово» (Bindings::SendHost); and the
// game's own grass and sea detail (scene density the renderer preset does not cover), and the frame rate
// «Частота кадров» among the rates the host reports for the display (applied at once). The
// single fx_* effect overrides are developer-only engine.ini keys (and STORM_FX_OPTIONS).
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
    {"SEA_DETAILS_SLIDE", "slider"},
    {"HUD_SLIDE", "slider"},
    {"AUTOSAVE_CHECKBOX", "check"},
    {"DIRECTSAIL_CHECKBOX", "check"},
    {"ENHANCEDSAILING_CHECKBOX", "check"},
    {"INVERT_MOUSE_CHECKBOX", "check"},
    {"ALWAYS_RUN_CHECKBOX", "check"},
    {"CAMERASWING_CHECKBOX", "check"},
    {"HERB_CHECKBOX", "check"},
    {"QUESTMARK_CHECKBOX", "check"},
    {"SHIPMARK_CHECKBOX", "check"},
    {"BATTLE_MODE_CHECKBOX", "check"},
    {"HUD_DESCRIP_TEXT", "text"},
    // Not offered: colour correction (unavailable in the Android renderer), keyboard
    // bindings (mobile only), the game's effects the quality preset decides (glow, sky
    // rotation, dynamic lights, simple sea, crew on deck), the game's own captions and
    // pictures of its layout.
    {"GLOW_SLIDE", "slider"},
    {"ROTATESKY_CHECKBOX", "check"},
    {"DYNAMICLIGHTS_CHECKBOX", "check"},
    {"SIMPLESEA_CHECKBOX", "check"},
    {"CREWONDECK_CHECKBOX", "check"},
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
            Column graphics{bindings.Text("settings.graphics")}, detail{bindings.Text("settings.detail")};
            // The preset's three names stand right under the column's title «Графика».
            Segment(bindings, graphics, "fx_preset", "settings.graphics", {0, 1, 2},
                    {bindings.Text("settings.fx_low"), bindings.Text("settings.fx_medium"), bindings.Text("settings.fx_high")}, false);
            graphics.rows.back().erase("label");
            // The automatic preset's rung below «Низкая»: «Низкая» with the scene at a lower height, which only the
            // measurement chooses (fx_auto_render_height, while that choice stands); its height under the row.
            if (Known("fx_auto_render_height") && draft_["fx_auto_render_height"].get<int>() > 0 && Known("fx_preset") &&
                draft_["fx_preset"].get<int>() == 0)
                graphics.rows.back()["hint"] =
                    Fill(bindings.Text("settings.fx_scene_height"), {std::to_string(draft_["fx_auto_render_height"].get<int>())});
            Segment(bindings, graphics, "texture_degradation", "settings.texture_quality", {0, 1, 2},
                    {bindings.Text("settings.texture_full"), bindings.Text("settings.texture_medium"), bindings.Text("settings.texture_low")});
            // «Частота кадров»: the rates the host found the display can show (frame_rate_options: whole divisors
            // of its refresh rates), applied at once with «Готово» (the engine's frame pacing).
            std::vector<int> rates;
            Json rateLabels = Json::array();
            if (draft_.is_object() && draft_.contains("frame_rate_options") && draft_["frame_rate_options"].is_array())
                for (const Json &rate : draft_["frame_rate_options"])
                    if (rate.is_number_integer())
                    {
                        rates.push_back(rate.get<int>());
                        rateLabels.push_back(std::to_string(rates.back()));
                    }
            Segment(bindings, graphics, "frame_rate", "settings.frame_rate", rates, rateLabels, false);
            game(detail, "HERB_CHECKBOX", "settings.grass");
            game(detail, "SEA_DETAILS_SLIDE", "settings.sea_detail");
            sections.push_back({"", {graphics, detail}});
            // A debuggable app reports single fx overrides (GraphicsSettings.DEBUG_KEYS): rows to bisect
            // rendering problems on a phone, applied with «Готово» like the preset. Development only, so
            // the texts are not translated.
            if (Known("fx_ssao"))
            {
                Column debug{"Отладка эффектов"};
                const Json onOff = Json::array({"пресет", "выкл", "вкл"});
                DebugSegment(bindings, debug, "fx_ssao", "SSAO", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_msaa", "MSAA", {-1, 1, 2, 4}, {"пресет", "выкл", "2x", "4x"});
                DebugSegment(bindings, debug, "fx_bloom", "Свечение (bloom)", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_grade", "Цветокоррекция", {-1, 0, 1, 2}, {"пресет", "выкл", "Карибы", "Пасмурно"});
                DebugSegment(bindings, debug, "fx_dither", "Дизеринг", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_contact_shadows", "Контактные тени", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_shadows", "Тени солнца", {-1, 0, 1, 2}, {"пресет", "выкл", "низк", "мягк"});
                DebugSegment(bindings, debug, "fx_sea_band_limit", "Море: мягкая даль", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_sea_soft_edges", "Море: мягкие края", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_sea_specular", "Море: блик солнца", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_light_per_pixel", "Освещение попиксельно", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_light_point", "Точечные огни", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_light_specular", "Блики", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_light_normal_map", "Карты нормалей", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_alpha_to_coverage", "Alpha to coverage", {-1, 0, 1}, onOff);
                DebugSegment(bindings, debug, "fx_anisotropy", "Анизотропия", {-1, 1, 4, 8}, {"пресет", "выкл", "4x", "8x"});
                DebugSegment(bindings, debug, "fx_ao_volume", "Затенение (объём)", {-1, 0, 1, 2, 3}, {"пресет", "выкл", "игра", "умер", "сильн"});
                // FSR 1 and SMAA (cc59cbe9, 924e22d2): the scene's internal height (native on displays not taller),
                // the post AA, RCAS;
                // «Замер GPU» logs GPU timer averages, which «Поделиться логом» carries out.
                if (Known("fx_render_height"))
                {
                    DebugSegment(bindings, debug, "fx_render_height", "Высота сцены", {-1, 720, 900, 1000, 1100, 0},
                                 {"пресет", "720", "900", "1000", "1100", "экран"});
                    DebugSegment(bindings, debug, "fx_smaa", "SMAA", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_sharpen", "Резкость (RCAS)", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_gpu_profile", "Замер GPU", {-1, 1}, {"выкл", "вкл"});
                }
                // The lamps' relight of locations (f0265528, fb6f189e): how many lamps cast shadows, the
                // shadow filter, how much brighter characters indoors are than the room, the lamp views.
                if (Known("fx_lamp_shadows"))
                {
                    DebugSegment(bindings, debug, "fx_lamp_shadows", "Тени ламп", {-1, 0, 8, 16}, {"пресет", "выкл", "8", "16"});
                    DebugSegment(bindings, debug, "fx_lamp_soft", "Фильтр теней ламп", {-1, 0, 1}, {"пресет", "3x3", "5x5"});
                    DebugSegment(bindings, debug, "fx_lamp_model_lift", "Персонажи ярче комнаты", {-1, 0, 20, 50, 100},
                                 {"пресет", "0%", "20%", "50%", "100%"});
                    DebugSegment(bindings, debug, "fx_lamp_debug", "Лампы: отладка", {-1, 0, 1, 2}, {"пресет", "выкл", "цвета", "маска"});
                }
                // The share of a lamp's own colour in the light it adds to baked surfaces (7f1592e6).
                if (Known("fx_lamp_tint"))
                    DebugSegment(bindings, debug, "fx_lamp_tint", "Цвет ламп на стенах", {-1, 0, 25, 50, 100},
                                 {"пресет", "0%", "25%", "50%", "100%"});
                // The one falloff of every light (fx/light_falloff.hpp): how far it reaches, its soft core, its level.
                if (Known("fx_light_reach"))
                {
                    DebugSegment(bindings, debug, "fx_light_reach", "Огни: дальность", {-1, 100, 160, 220, 300},
                                 {"пресет", "100%", "160%", "220%", "300%"});
                    DebugSegment(bindings, debug, "fx_light_core", "Огни: мягкость у огня", {-1, 10, 40, 80},
                                 {"пресет", "10 см", "40 см", "80 см"});
                    DebugSegment(bindings, debug, "fx_light_level", "Огни: яркость", {-1, 50, 70, 85, 100},
                                 {"пресет", "50%", "70%", "85%", "100%"});
                }
                // Sunlight through the sails (fe7b54e5): its strength.
                if (Known("fx_sail_light"))
                    DebugSegment(bindings, debug, "fx_sail_light", "Свет сквозь паруса", {-1, 0, 100, 200, 300},
                                 {"пресет", "выкл", "100%", "200%", "300%"});
                // Our town lightmaps for the live sun (effects/town_light.hpp) against the game's, and what they hold.
                if (Known("fx_town_light"))
                {
                    DebugSegment(bindings, debug, "fx_town_light", "Свет городов", {-1, 0, 1}, {"пресет", "игра", "наш"});
                    DebugSegment(bindings, debug, "fx_town_light_debug", "Свет городов: отладка", {-1, 0, 1}, onOff);
                }
                // One bounce of indirect light (9c8b33fe) and the town walls relit per pixel (32d9ce5d).
                if (Known("fx_bounce"))
                {
                    DebugSegment(bindings, debug, "fx_bounce", "Отражённый свет", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_bounce_debug", "Отражённый свет: отладка", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_town_walls", "Стены городов", {-1, 0, 1}, {"пресет", "игра", "наши"});
                }
                // Gloss maps (6e5576d5, Высокая): per-texel highlights of the sun and lamps, metal in its own colour.
                if (Known("fx_gloss"))
                {
                    DebugSegment(bindings, debug, "fx_gloss", "Блеск", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_gloss_strength", "Сила блеска", {-1, 50, 100, 200, 300},
                                 {"пресет", "50%", "100%", "200%", "300%"});
                    DebugSegment(bindings, debug, "fx_gloss_debug", "Блеск: отладка", {-1, 0, 1, 2, 3, 4},
                                 {"пресет", "выкл", "блеск", "нормали", "солнце", "металл"});
                }
                // Waves from objects (49b75861): ripples, their lift of the surface, the ships' wakes, their views.
                if (Known("fx_sea_waves"))
                {
                    DebugSegment(bindings, debug, "fx_sea_waves", "Море: рябь от объектов", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_sea_waves_lift", "Море: рябь поднимает воду", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_sea_wakes", "Море: кильватер", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "fx_sea_waves_debug", "Море: волны, отладка", {-1, 0, 1, 2},
                                 {"пресет", "выкл", "слои", "наклон"});
                }
                // The parallax probe (engine effects/parallax_probe.hpp): the relief texture under the screen centre, its
                // parallax and trust mask drawn over the picture; the second row switches that texture's parallax.
                // Settings reach the engine as stored values applied with «Готово», not as events: every change of
                // fx_pom_toggle's value is one press (the engine switches on the change, never on the value itself).
                if (Known("fx_pom_probe"))
                {
                    DebugSegment(bindings, debug, "fx_pom_probe", "Прицел: текстура и параллакс", {-1, 1}, {"выкл", "вкл"});
                    DebugSegment(bindings, debug, "fx_pom_toggle", "Прицел: переключить параллакс", {-1, 1}, {"А", "Б"});
                }
                // Neural ×2 textures near the camera (feat/upscale, Высокая): forced on or off.
                if (Known("fx_upscale"))
                    DebugSegment(bindings, debug, "fx_upscale", "Нейро-×2 текстур", {-1, 0, 1}, onOff);
                // The renderer's thread, its frame overlap and the graphics API (read at the game's start).
                if (Known("render_backend"))
                {
                    DebugSegment(bindings, debug, "render_backend", "Графический API", {-1, 1, 2}, {"авто", "GLES", "Vulkan"});
                    DebugSegment(bindings, debug, "render_thread", "Поток рендера", {-1, 0, 1}, onOff);
                    DebugSegment(bindings, debug, "render_overlap", "Кадр вперёд", {-1, 0, 1}, {"по кадру", "никогда", "всегда"});
                }
                sections.push_back({"", {debug}});
            }
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
                // The settings only: what the host reports about the device (frame_rate_options), whether the preset
                // was measured (fx_preset_auto: the host's automatic choice) and the measured scene height
                // (fx_auto_render_height) are not ones.
                Json settings = draft_;
                settings.erase("frame_rate_options");
                settings.erase("fx_preset_auto");
                settings.erase("fx_auto_render_height");
                bindings.SendHost("app-settings", settings.dump());
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

    // `restart`: the option applies at the next start (marked so); `stack`: the options go
    // under the label in a full-width segment.
    void Segment(Bindings &bindings, Column &column, const char *key, const char *label, std::vector<int> values, Json options,
                 bool restart = true, bool stack = true)
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
                                   {"stack", stack},
                                   {"restart", restart},
                                   {"disabled", !Known(key)},
                                   {"reason", Known(key) ? std::string() : bindings.Text("settings.app_unavailable")}});
        bindings.On(id, "change", [this, &bindings, key, values](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < values.size() && Known(key))
                draft_[key] = values[index];
            bindings.Rebuild();
        });
    }

    // A debug override row (raw, untranslated label); applied with «Готово» without a restart.
    void DebugSegment(Bindings &bindings, Column &column, const char *key, const char *label, std::vector<int> values, Json options)
    {
        int selected = 0;
        for (size_t i = 0; i < values.size(); ++i)
            if (draft_[key].get<int>() == values[i])
                selected = static_cast<int>(i);
        const std::string id = std::string("app-") + key;
        column.rows.push_back(Json{{"kind", "segment"}, {"id", id},       {"label", label},     {"options", options},
                                   {"selected", selected}, {"stack", true}, {"restart", false}, {"disabled", false}});
        bindings.On(id, "change", [this, &bindings, key, values](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < values.size())
                draft_[key] = values[index];
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
