#include "mobile_image_cache.hpp"
#include "mobile_widget.hpp"
#include "mobile_image_bridge.hpp"
#include <cassert>
#include <iostream>
using namespace storm::mobile;
int main()
{
    // Stable handle per picture, screen invalidation, UV changes, a bounded registry.
    ImageCache cache;
    cache.screen(4);
    auto a = cache.reference({"INTERFACES\\test.tga", {}});
    auto same = cache.reference({"resource/textures/interfaces/test.tga", {}});
    assert(a.handle && a.handle == same.handle);
    // Optional semantic identity belongs to each reference, not the bitmap cache.
    assert(MobileImageJson(a).find("\"source\"") == std::string::npos);
    auto atlas = a;
    atlas.source = {"atlas", "ICONS_SPEC", "FencingL skill icon", {}, {}, -1};
    assert(MobileImageJson(atlas).find("\"source\":{\"kind\":\"atlas\",\"group\":\"ICONS_SPEC\",\"image\":\"FencingL skill icon\"}") != std::string::npos);
    assert(atlas.handle == same.handle && same.source.kind.empty());
    auto escaped = atlas;
    escaped.source.image = "quote\"\\\n";
    assert(MobileImageJson(escaped).find("quote\\\"\\\\\\u000a") != std::string::npos);
    // A picture the engine names by file: its texture-namespace name and the rectangle shown.
    assert(MobileTextureName("Interfaces\\Maps\\Kayman.tga") == "interfaces/maps/kayman");
    assert(MobileTextureName("resource/textures/loading/inside/kiss.tga.tx") == "loading/inside/kiss");
    assert(MobileTextureName("../secret.tga").empty() && MobileTextureName("C:/x.tga").empty());
    auto texture = a;
    texture.source = {"texture", {}, "interfaces/maps/kayman", {}, {}, -1, {0.f, 0.25f, 0.5f, 1.f}};
    assert(MobileImageJson(texture).find("\"source\":{\"kind\":\"texture\",\"image\":\"interfaces/maps/kayman\",\"uv\":[0,0.25,0.5,1]}") != std::string::npos);
    auto battle = WithBattleIconSource(a, "land", "LandTarget_SLB", 16);
    assert(MobileImageJson(battle).find("\"atlas\":\"LandTarget_SLB\",\"pic\":16") != std::string::npos);
    assert(WithBattleIconSource({}, "land", "LandTarget_SLB", 16).source.kind.empty());
    assert(WithBattleIconSource(a, "land", "LandTarget_SLB", -1).source.kind.empty());
    auto changed = cache.reference({"interfaces/test.tga", {0, 0, .5f, 1}});
    assert(a.handle != changed.handle);
    assert(!cache.reference({"../outside", {}}).handle);
    assert(!cache.reference({"/absolute", {}}).handle);
    cache.screen(5);
    assert(cache.reference({"interfaces/test.tga", {}}).handle != a.handle);
    for (int i = 0; i < 4100; ++i)
        cache.reference({"interfaces/" + std::to_string(i), {}});
    assert(cache.descriptors() == 4096);
    MobileWidget widget{"TABLE", "choice", "old label", "1", 2, 0, true};
    widget.rowId = "Hull";
    widget.cells.push_back({0, "red", 0xff123456, {a}});
    widget.actions = {"select", "activate"};
    auto json = MobileWidgetJson("ship", {widget}, 4, 7, "english");
    assert(json.find("\"screenRevision\":4") != std::string::npos &&
           json.find("\"rowId\":\"Hull\"") != std::string::npos &&
           json.find("\"label\":\"old label\"") != std::string::npos && json.find("4279383126") != std::string::npos);
    std::cout
        << "PASS mobile images: picture handles, screen lifetimes, bounds, sources and schema\n";
}
