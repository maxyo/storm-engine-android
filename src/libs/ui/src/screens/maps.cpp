// The game's maps (interfaces "mapview" — the atlas, "mapbest" — the excellent map, "papermap";
// mockups V2-Extra-MapView, -MapBest, -PaperMap and their -Colony variants): the map picture
// in c-map-view (pan, pinch, zoom buttons; the game's markers on it), the atlas's list of maps
// on the left, and the colony the player tapped in a C-ColonySheet on the right. Fills the pack
// screen <s-map>.
//
// The engine exports the scrolled picture MAP as a "map" widget (texture, base size, the view
// the game shows) and its imagelist as "map_marker" rows (xinterface CXI_SCROLLEDPICTURE). A
// tap goes back as "point" in the picture's base units: the engine turns it into the right
// click the scripts read (SelectRColony), so each script finds the colony with its own maths.
// The game shows the colony while the right button is held; "release" lets go of it.
//
// The colony window's nodes differ per script: INFO_CAPTION and INFO_NATION_PICTURE head it,
// COLONY_INFO_TEXT rows pair with COLONY_INFO_TEXT2 (or COLONY_INFO) rows as facts, and every
// <PREFIX>_CAPTION opens a section that collects the <PREFIX>… pictures (goods, paired with the
// <PREFIX>_INFO rows) and texts.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"MAP", "map|map_marker", true},
    {"TITLE", "heading|text"},
    {"EXIT_BTN", "button"},
    {"TABLE_MAPS", "choice"},
    {"STR_1", "text"},
    {"MAPS_LIST", "text"},
    {"INFO_CAPTION", "text|heading"},
    {"INFO_NATION_PICTURE", "image"},
    {"COLONY_*", "text"},
    {"IMPORT*", "text|image"},
    {"EXPORT*", "text|image"},
    {"CONTRABAND_CAPTION", "text"},
    {"AGGRESSIVE_CAPTION", "text"},
    {"TRADEASSISTANT_*", "text"},
    {"GOODS_PICTURES", "image"}, // MapBest: the goods of the colony and of the trade assistant, where the script drew them
    {"MAIN_BACK", "image"},
    {"BOUNDARIES", "image"},
};

// Sections of the colony window in the order the game lays them out.
constexpr const char *kSections[] = {"EXPORT", "IMPORT", "CONTRABAND", "AGGRESSIVE", "TRADEASSISTANT"};

bool Starts(const std::string &node, std::string_view prefix)
{
    return Upper(node).rfind(prefix, 0) == 0;
}

std::vector<std::string> Labels(const std::vector<const Widget *> &rows)
{
    std::vector<std::string> out;
    for (const Widget *row : rows)
        if (!row->label.empty())
            out.push_back(row->label);
    return out;
}

class MapScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-map";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }
    bool OwnsBack() const override
    {
        return info_open_;
    }
    void Back(Bindings &bindings) override
    {
        if (map_)
            bindings.Send(*map_, "release");
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *map = nullptr;
        std::vector<const Widget *> markers;
        for (const Widget *widget : admitted.All("MAP"))
        {
            if (widget->type == "map")
                map = widget;
            else
                markers.push_back(widget);
        }
        map_ = map;
        Json view = Json::object();
        std::string title = admitted.Label("TITLE");
        view["title"] = title.empty() ? bindings.Text("map.title") : title;
        Json picture = map ? MapViewOf(*map, markers) : Json::object();
        // MapBest.ini draws the map as the picture MAIN_BACK under a transparent MAP (the
        // markers and the clicks); the scripts measure it in MAP's base units all the same.
        if (map && picture["texture"] == "")
            if (const Widget *back = admitted.One("MAIN_BACK"); back && !TextureOf(back->FirstImage()).is_null())
            {
                const Json texture = TextureOf(back->FirstImage());
                picture["texture"] = texture["texture"];
                picture["uv"] = texture["uv"];
            }
        if (!map || picture["texture"] == "")
            bindings.Error("map: the engine sent no picture for MAP (nor MAIN_BACK)");
        if (map && picture["base"].empty())
        {
            bindings.Error("map: MAP has no base size in its context; drawn as 1000 x 1000");
            picture["base"] = Json::array({1000, 1000});
        }
        view["map"] = picture;
        if (map)
            bindings.On("map", "point", [&bindings, map](const Event &event) {
                if (map->Offers("point"))
                    bindings.Send(*map, "point", event.value);
            });

        // The atlas: one row per map the hero carries.
        const std::vector<const Widget *> maps = admitted.All("TABLE_MAPS");
        Json rows = Json::array();
        for (const Widget *row : maps)
            rows.push_back(Json{{"cells", Json::array({Json{{"text", row->CellText(0)}, {"icon", "map"}}})},
                                {"selected", row->value == "1"},
                                {"disabled", !row->enabled}});
        view["maps"] = rows;
        view["mapColumns"] = Json::array({Json{{"w", "1"}}});
        view["mapsTitle"] = admitted.Label("STR_1");
        bindings.On("maps", "row", [&bindings, maps](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < maps.size())
                TapChoice(bindings, *maps[index]);
        });

        view["info"] = Info(admitted);
        info_open_ = !view["info"].is_null();
        if (info_open_)
        {
            // Pages of ours instead of one scrolling sheet: what the colony trades, the trade assistant's
            // profits, and the town's facts (with the siege). A page with nothing on it is not offered; a new
            // colony opens on the first there is.
            Json &info = view["info"];
            if (info["title"] != info_title_)
            {
                info_title_ = info["title"].get<std::string>();
                info_page_.clear();
            }
            const auto has = [&info](const char *page) {
                for (const Json &section : info["sections"])
                    if (section["page"] == page)
                        return true;
                return false;
            };
            Json tabs = Json::array();
            const auto offer = [&](const char *id, const std::string &label, bool there) {
                if (there)
                    tabs.push_back(Json{{"id", id}, {"label", label}, {"selected", false}});
            };
            offer("trade", bindings.Text("colony.trade"), has("trade"));
            offer("assistant", bindings.Text("colony.assistant"), has("assistant"));
            offer("city", bindings.Text("colony.city"), !info["facts"].empty() || !info["notes"].empty());
            std::string page = tabs.empty() ? std::string("trade") : tabs[0]["id"].get<std::string>();
            for (const Json &tab : tabs)
                if (tab["id"] == info_page_)
                    page = info_page_;
            for (Json &tab : tabs)
                tab["selected"] = tab["id"] == page;
            info["page"] = page;
            info["tabs"] = tabs.size() > 1 ? tabs : Json::array();
            bindings.On("info-tabs", "select", [this, &bindings](const Event &event) {
                info_page_ = event.value;
                bindings.Rebuild();
            });
        }
        else
            info_title_.clear();
        bindings.On("info-close", "", [this, &bindings](const Event &) { Back(bindings); });

        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // The colony the right click picked, or null while the game shows none.
    Json Info(const Admission &admitted) const
    {
        const std::string caption = admitted.Label("INFO_CAPTION");
        if (caption.empty())
            return Json();
        Json info{{"title", caption}};
        const Widget *flag = admitted.One("INFO_NATION_PICTURE");
        const std::string nation = NationOf(flag ? flag->FirstImage() : nullptr);
        info["nation"] = nation.empty() ? "" : "nation-" + nation;
        std::string sub;
        for (const char *node : {"COLONY_TRAVEL_INFO", "COLONY_TIME_INFO"})
            for (const std::string &line : Labels(admitted.All(node)))
                sub += (sub.empty() ? "" : " · ") + line;
        info["sub"] = sub;

        const std::vector<std::string> names = Labels(admitted.All("COLONY_INFO_TEXT"));
        std::vector<std::string> values = Labels(admitted.All("COLONY_INFO_TEXT2"));
        if (values.empty())
            values = Labels(admitted.All("COLONY_INFO"));
        Json facts = Json::array();
        for (size_t i = 0; i < names.size(); ++i)
            facts.push_back(Json{{"k", names[i]}, {"v", i < values.size() ? values[i] : std::string()}});
        info["facts"] = facts;
        info["factsTitle"] = admitted.Label("COLONY_INFO_LABEL");
        info["notes"] = Labels(admitted.All("COLONY_INFO_SIEGE"));

        Json sections = Json::array();
        for (const char *prefix : kSections)
        {
            const std::string caption_node = std::string(prefix) + "_CAPTION";
            std::vector<const Widget *> pictures, texts;
            for (const Widget *widget : admitted.known)
                if (Starts(widget->node, prefix) && Upper(widget->node) != caption_node)
                    (widget->type == "image" ? pictures : texts).push_back(widget);
            std::sort(pictures.begin(), pictures.end(), [](const Widget *a, const Widget *b) { return Upper(a->node) < Upper(b->node); });
            const std::vector<std::string> goods_names = Labels(admitted.All(std::string(prefix) + "_INFO"));
            Json goods = Json::array();
            for (size_t i = 0; i < pictures.size(); ++i)
            {
                Json texture = TextureOf(pictures[i]->FirstImage());
                if (texture.is_null())
                    continue;
                texture["label"] = i < goods_names.size() ? goods_names[i] : std::string();
                goods.push_back(texture);
            }
            Json lines = Json::array();
            for (const Widget *text : texts)
                if (!text->label.empty() && (goods.empty() || Upper(text->node) != std::string(prefix) + "_INFO"))
                    lines.push_back(text->label);
            // A caption alone says nothing (MapBest draws its goods as an image collection the
            // engine does not export): the section is left out.
            const std::string title = admitted.Label(caption_node);
            if (goods.empty() && lines.empty())
                continue;
            sections.push_back(Json{{"title", title}, {"goods", goods}, {"lines", lines}, {"page", std::string(prefix) == "TRADEASSISTANT" ? "assistant" : "trade"}});
        }
        // MapBest.c draws its goods as pictures at fixed places (ShowColonyInfo): the colony's own right of
        // x 410 in bands (export from y 245, import 330, contraband 415, aggressive 470), the trade assistant's
        // in two columns (x 230: bought here and sold there, x 310: the other way), one row per price line of
        // TRADEASSISTANT_PRICES1 / 2 (profit per unit of weight). The engine exports each picture with its
        // place and the good's name; here they become rows: the good, and for the assistant its profit beside it.
        const std::vector<const Widget *> pictures = admitted.All("GOODS_PICTURES");
        if (!pictures.empty())
        {
            const auto place = [](const Widget *picture, const char *axis) {
                const auto found = picture->context.find(axis);
                return found == picture->context.end() ? 0 : std::atoi(found->second.c_str());
            };
            const auto good = [](const Widget *picture) {
                Json texture = TextureOf(picture->FirstImage());
                if (texture.is_null())
                    texture = Json::object();
                texture["label"] = picture->label.empty() ? picture->row_id : picture->label;
                return texture;
            };
            const auto numbers = [&admitted](const char *node) {
                std::vector<std::string> out;
                for (const std::string &text : Labels(admitted.All(node)))
                    for (size_t at = 0; at < text.size();)
                    {
                        const size_t end = text.find_first_of(" \n\t", at);
                        if (end != at)
                            out.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
                        at = end == std::string::npos ? text.size() : end + 1;
                    }
                return out;
            };
            sections = Json::array();
            struct Band
            {
                const char *caption;
                int from, to;
            };
            static const Band bands[] = {{"EXPORT_CAPTION", 0, 330}, {"IMPORT_CAPTION", 330, 415}, {"CONTRABAND_CAPTION", 415, 470}, {"AGGRESSIVE_CAPTION", 470, 1 << 20}};
            for (const Band &band : bands)
            {
                Json goods = Json::array();
                for (const Widget *picture : pictures)
                    if (place(picture, "x") >= 400 && place(picture, "y") >= band.from && place(picture, "y") < band.to)
                        goods.push_back(good(picture));
                if (!goods.empty())
                    sections.push_back(Json{{"title", admitted.Label(band.caption)}, {"goods", goods}, {"lines", Json::array()}, {"page", "trade"}});
            }
            for (int column = 0; column < 2; ++column)
            {
                std::vector<const Widget *> listed;
                for (const Widget *picture : pictures)
                    if (place(picture, "x") < 400 && (place(picture, "x") >= 270) == (column == 1))
                        listed.push_back(picture);
                std::stable_sort(listed.begin(), listed.end(), [&place](const Widget *a, const Widget *b) { return place(a, "y") < place(b, "y"); });
                const std::vector<std::string> prices = numbers(column == 0 ? "TRADEASSISTANT_PRICES1" : "TRADEASSISTANT_PRICES2");
                Json goods = Json::array();
                for (size_t i = 0; i < listed.size(); ++i)
                {
                    Json row = good(listed[i]);
                    row["value"] = i < prices.size() ? "+" + prices[i] : std::string();
                    goods.push_back(std::move(row));
                }
                const std::string direction = admitted.Label(column == 0 ? "TRADEASSISTANT_TO" : "TRADEASSISTANT_FROM");
                if (!goods.empty())
                    sections.push_back(Json{{"title", direction}, {"goods", goods}, {"lines", Json::array()}, {"page", "assistant"}});
            }
            if (const std::string special = admitted.Label("TRADEASSISTANT_SPECIAL"); !special.empty())
                sections.push_back(Json{{"title", ""}, {"goods", Json::array()}, {"lines", Json::array({special})}, {"page", "assistant"}});
        }
        info["sections"] = sections;
        return info;
    }

    const Widget *map_ = nullptr;
    bool info_open_ = false;
    std::string info_title_; // the colony the sheet shows
    std::string info_page_;  // the page of it the player chose
};

} // namespace

std::unique_ptr<Screen> CreateMapScreen()
{
    return std::make_unique<MapScreen>();
}

} // namespace storm::ui::screens
