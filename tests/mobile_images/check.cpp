#include "mobile_image_cache.hpp"
#include "mobile_image_decode.hpp"
#include "mobile_widget.hpp"
#include "mobile_image_bridge.hpp"
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
using namespace storm::mobile;
using Bytes = std::vector<uint8_t>;
void w16(Bytes &b, size_t p, uint16_t v)
{
    b[p] = v;
    b[p + 1] = v >> 8;
}
void w32(Bytes &b, size_t p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b[p + i] = v >> (i * 8);
}
Bytes tx(uint32_t w, uint32_t h, uint32_t format, Bytes payload)
{
    Bytes b(24);
    w32(b, 4, w);
    w32(b, 8, h);
    w32(b, 12, 1);
    w32(b, 16, format);
    w32(b, 20, payload.size());
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
void pixel(const Pixels &p, int x, int y, std::array<uint8_t, 4> expected)
{
    for (int c = 0; c < 4; ++c)
        assert(p.rgba[(y * p.width + x) * 4 + c] == expected[c]);
}
int main(int argc, char **argv)
{
    if (argc == 4 && std::string(argv[1]) == "--decode")
    {
        std::ifstream f(argv[2], std::ios::binary);
        Bytes b((std::istreambuf_iterator<char>(f)), {});
        auto r = Decode(b, "tx");
        if (!r)
        {
            std::cerr << r.error << '\n';
            return 1;
        }
        std::ofstream out(argv[3], std::ios::binary);
        out.write(reinterpret_cast<const char *>(r.pixels.rgba.data()), r.pixels.rgba.size());
        std::cout << r.pixels.width << ' ' << r.pixels.height << '\n';
        return 0;
    }
    auto rgba = Decode(tx(2, 2, 21, {3, 2, 1, 4, 30, 20, 10, 40, 90, 80, 70, 60, 190, 180, 170, 160}), "tx");
    assert(rgba);
    pixel(rgba.pixels, 0, 0, {1, 2, 3, 4});
    pixel(rgba.pixels, 1, 1, {170, 180, 190, 160});
    pixel(Decode(tx(1, 1, 22, {3, 2, 1, 0}), "tx").pixels, 0, 0, {1, 2, 3, 255});
    pixel(Decode(tx(1, 1, 23, {0, 0xf8}), "tx").pixels, 0, 0, {255, 0, 0, 255});
    pixel(Decode(tx(1, 1, 25, {0x1f, 0}), "tx").pixels, 0, 0, {0, 0, 255, 0});
    pixel(Decode(tx(1, 1, 26, {0x23, 0xa1}), "tx").pixels, 0, 0, {17, 34, 51, 170});
    pixel(Decode(tx(1, 1, 50, {72}), "tx").pixels, 0, 0, {72, 72, 72, 255});
    Bytes bc1 = {0, 0xf8, 0xe0, 7, 0xe4, 0xe4, 0xe4, 0xe4};
    auto bc = Decode(tx(3, 2, 0x31545844, bc1), "tx");
    assert(bc);
    pixel(bc.pixels, 0, 0, {255, 0, 0, 255});
    pixel(bc.pixels, 1, 0, {0, 255, 0, 255});
    pixel(bc.pixels, 2, 0, {170, 85, 0, 255});
    auto transparent = bc1;
    w16(transparent, 0, 0);
    w16(transparent, 2, 0xffff);
    w32(transparent, 4, 0xffffffff);
    pixel(Decode(tx(4, 4, 0x31545844, transparent), "tx").pixels, 3, 3, {0, 0, 0, 0});
    Bytes bc3(8, 0x72);
    bc3.insert(bc3.end(), bc1.begin(), bc1.end());
    auto d3 = Decode(tx(4, 4, 0x33545844, bc3), "tx");
    pixel(d3.pixels, 0, 0, {255, 0, 0, 34});
    pixel(d3.pixels, 1, 0, {0, 255, 0, 119});
    Bytes bc5 = {200, 60, 0x88, 0xc6, 0xfa, 0x88, 0xc6, 0xfa};
    bc5.insert(bc5.end(), bc1.begin(), bc1.end());
    auto d5 = Decode(tx(4, 4, 0x35545844, bc5), "tx");
    assert(d5);
    assert(d5.pixels.rgba[3] == 200);
    assert(d5.pixels.rgba[7] == 60);
    assert(d5.pixels.rgba[11] == 180);
    // BC5 low-alpha branch and DDS compressed edge/mip validation.
    Bytes low = {0, 255, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    low.insert(low.end(), bc1.begin(), bc1.end());
    pixel(Decode(tx(4, 4, 0x35545844, low), "tx").pixels, 0, 0, {255, 0, 0, 255});
    Bytes compressed(128);
    w32(compressed, 0, 0x20534444);
    w32(compressed, 4, 124);
    w32(compressed, 12, 2);
    w32(compressed, 16, 3);
    w32(compressed, 76, 32);
    w32(compressed, 80, 4);
    w32(compressed, 84, 0x31545844);
    compressed.insert(compressed.end(), bc1.begin(), bc1.end());
    assert(Decode(compressed, "dds"));
    w32(compressed, 28, 2);
    assert(!Decode(compressed, "dds"));
    compressed.insert(compressed.end(), bc1.begin(), bc1.end());
    assert(Decode(compressed, "dds"));
    // All four TGA origin combinations must produce identical canonical top-left pixels.
    for (int origin : {0, 16, 32, 48})
    {
        Bytes t(18);
        t[2] = 2;
        w16(t, 12, 2);
        w16(t, 14, 2);
        t[16] = 32;
        t[17] = origin | 8;
        for (int n = 0; n < 4; ++n)
        {
            int x = n % 2, y = n / 2;
            if (origin & 16)
                x = 1 - x;
            if (!(origin & 32))
                y = 1 - y;
            int k = y * 2 + x;
            for (int c : {2, 1, 0, 3})
                t.push_back(rgba.pixels.rgba[k * 4 + c]);
        }
        auto p = Decode(t, "tga");
        assert(p && p.pixels.rgba == rgba.pixels.rgba);
    }
    Bytes rle(18);
    rle[2] = 10;
    w16(rle, 12, 3);
    w16(rle, 14, 1);
    rle[16] = 24;
    rle[17] = 32;
    rle.insert(rle.end(), {0x82, 3, 2, 1});
    auto rr = Decode(rle, "tga");
    assert(rr);
    pixel(rr.pixels, 2, 0, {1, 2, 3, 255});
    rle[18] = 0x83;
    assert(!Decode(rle, "tga"));
    // DDS padded rows must not be interpreted as pixels, masks can change channel order.
    Bytes dds(128 + 16, 0);
    w32(dds, 0, 0x20534444);
    w32(dds, 4, 124);
    w32(dds, 8, 8);
    w32(dds, 12, 2);
    w32(dds, 16, 1);
    w32(dds, 20, 8);
    w32(dds, 76, 32);
    w32(dds, 80, 0x41);
    w32(dds, 88, 32);
    w32(dds, 92, 0xff);
    w32(dds, 96, 0xff00);
    w32(dds, 100, 0xff0000);
    w32(dds, 104, 0xff000000);
    w32(dds, 128, 0x04030201);
    w32(dds, 136, 0x281e140a);
    auto dd = Decode(dds, "dds");
    assert(dd);
    pixel(dd.pixels, 0, 0, {1, 2, 3, 4});
    pixel(dd.pixels, 0, 1, {10, 20, 30, 40});
    w32(dds, 20, 3);
    assert(!Decode(dds, "dds"));
    auto crop = CropResize(rgba.pixels, {.5f, 0, 1, 1}, 512);
    assert(crop && crop.pixels.width == 1 && crop.pixels.height == 2);
    pixel(crop.pixels, 0, 1, {170, 180, 190, 160});
    auto flip = CropResize(rgba.pixels, {1, 1, 0, 0}, 512);
    assert(flip);
    pixel(flip.pixels, 0, 0, {170, 180, 190, 160});
    auto half = CropResize(rgba.pixels, {.25f, .25f, .75f, .75f}, 512);
    assert(half && half.pixels.rgba == rgba.pixels.rgba);
    assert(CropResize(rgba.pixels, {}, 1).pixels.rgba == Bytes({170, 180, 190, 160}));
    assert(!CropResize(rgba.pixels, {-1, 0, 1, 1}, 10));
    assert(!CropResize(rgba.pixels, {0, 0, 0, 1}, 10));
    assert(!CropResize(rgba.pixels, {0, 0, std::numeric_limits<float>::quiet_NaN(), 1}, 10));
    assert(!CropResize(rgba.pixels, {}, 513));
    // Every truncation is rejected, including incomplete edge blocks and mip chains.
    auto encoded = tx(3, 2, 0x31545844, bc1);
    for (size_t n = 0; n < encoded.size(); ++n)
        assert(!Decode(Bytes(encoded.begin(), encoded.begin() + n), "tx"));
    auto mip = encoded;
    w32(mip, 12, 2);
    assert(!Decode(mip, "tx"));
    mip.insert(mip.end(), bc1.begin(), bc1.end());
    assert(Decode(mip, "tx"));
    auto bad = encoded;
    w32(bad, 4, 0xffffffff);
    assert(!Decode(bad, "tx"));
    bad = encoded;
    w32(bad, 20, 7);
    assert(!Decode(bad, "tx"));
    bad = encoded;
    w32(bad, 0, 2);
    assert(!Decode(bad, "tx"));
    // Stable immutable handle, screen invalidation, UV changes, shared LRU budget.
    ImageCache cache(32);
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
    auto battle = WithBattleIconSource(a, "land", "LandTarget_SLB", 16);
    assert(MobileImageJson(battle).find("\"atlas\":\"LandTarget_SLB\",\"pic\":16") != std::string::npos);
    assert(WithBattleIconSource({}, "land", "LandTarget_SLB", 16).source.kind.empty());
    assert(WithBattleIconSource(a, "land", "LandTarget_SLB", -1).source.kind.empty());
    auto changed = cache.reference({"interfaces/test.tga", {0, 0, .5f, 1}});
    assert(a.handle != changed.handle);
    assert(!cache.reference({"../outside", {}}).handle);
    assert(!cache.reference({"/absolute", {}}).handle);
    assert(!cache.descriptor(a.handle, a.imageRevision + 1));
    auto pixels = std::make_shared<Pixels>(rgba.pixels);
    cache.put("one", pixels);
    cache.put("two", pixels);
    assert(cache.bytes() == 32);
    assert(cache.get("one"));
    cache.put("three", pixels);
    assert(!cache.get("two") && cache.get("one"));
    cache.screen(5);
    assert(!cache.descriptor(a.handle, a.imageRevision) && cache.bytes() == 0);
    assert(cache.reference({"interfaces/test.tga", {}}).handle != a.handle);
    for (int i = 0; i < 4100; ++i)
        cache.reference({"interfaces/" + std::to_string(i), {}});
    assert(cache.descriptors() == 4096);
    ImageCache atlasCache(16);
    auto tiny = std::make_shared<Pixels>(Pixels{1, 1, {1, 2, 3, 4}});
    atlasCache.putPreview("atlas", pixels, "icon", tiny);
    assert(atlasCache.bytes() == 16 && atlasCache.get("atlas") && !atlasCache.get("icon"));
    ImageCache fittingCache(20);
    fittingCache.putPreview("atlas", pixels, "icon", tiny);
    assert(fittingCache.bytes() == 20 && fittingCache.get("atlas") && fittingCache.get("icon"));
    MobileWidget widget{"TABLE", "choice", "old label", "1", 2, 0, true};
    widget.rowId = "Hull";
    widget.cells.push_back({0, "red", 0xff123456, {a}});
    widget.actions = {"select", "activate"};
    auto json = MobileWidgetJson("ship", {widget}, 4, 7, "english");
    assert(json.find("\"screenRevision\":4") != std::string::npos &&
           json.find("\"rowId\":\"Hull\"") != std::string::npos &&
           json.find("\"label\":\"old label\"") != std::string::npos && json.find("4279383126") != std::string::npos);
    std::cout
        << "PASS mobile images: formats/channels/alpha/pitch/origins/BC edges/mips/crop/bounds/revisions/LRU/schema\n";
}
