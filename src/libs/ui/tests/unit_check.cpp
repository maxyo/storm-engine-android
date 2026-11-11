// Host checks for the parts of the UI runtime that need no GL context:
// the template language and the .tx decoder (synthetic data only).
#include "../src/components.h"
#include "../src/elements.h"
#include "storm_ui/tx_texture.h"
#include "../src/template.h"
#include "../src/text_pages.h"

#include <cstdio>
#include <cstring>

using storm::ui::Json;

static int failures = 0;

static void Expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static void ExpectText(const std::string &source, const Json &scope, const std::string &expected)
{
    std::string actual;
    try
    {
        actual = storm::ui::RenderTemplate(source, scope);
    }
    catch (const std::exception &error)
    {
        actual = std::string("<<") + error.what() + ">>";
    }
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", source.c_str(), expected.c_str(), actual.c_str());
        ++failures;
    }
}

static std::vector<uint8_t> TxHeader(uint32_t width, uint32_t height, uint32_t format)
{
    std::vector<uint8_t> data(24, 0);
    const uint32_t fields[6] = {0, width, height, 1, format, 0};
    std::memcpy(data.data(), fields, sizeof(fields));
    return data;
}

// The page rules of the dialogue speech (formerly DialoguePanel.PageBreak on Android):
// intermediate capacity, the final page reserving room for the replies, exact fit, a
// newline at the boundary, and reflow anchored at the page start.
static void Pages()
{
    using storm::ui::ChoosePage;
    const auto check = [](const std::string &text, size_t offset, std::vector<size_t> starts, std::vector<float> bottoms, float total,
                          float middle, float last, size_t next, size_t display, bool more, const char *what) {
        const storm::ui::PageBreak page = ChoosePage(text, offset, starts, bottoms, total, middle, last);
        Expect(page.next == next && page.display == display && page.more == more, what);
    };
    check("1111222233334444", 0, {0, 4, 8, 12, 16}, {10, 20, 30, 40}, 40, 30, 10, 12, 12, true, "pages: an intermediate page fills its height");
    check("11112222", 0, {0, 4, 8}, {10, 20}, 20, 30, 10, 4, 4, true, "pages: the last line is kept for the final page");
    check("11112222", 4, {0, 4}, {10}, 10, 30, 10, 8, 8, false, "pages: the final page fits beside the replies");
    check("111122223333", 0, {0, 4, 8, 12}, {10, 20, 30}, 30, 30, 30, 12, 12, false, "pages: an exact fit is one page");
    check("11112222", 0, {0, 4, 8}, {10, 20}, 20, 30, 20, 8, 8, false, "pages: all of it fits on the final page");
    check("aaa\nbbb\nccc", 0, {0, 4, 8, 11}, {10, 20, 30}, 30, 20, 10, 8, 7, true, "pages: a newline at the boundary shows no empty line");
    check("1111222233334444", 4, {0, 2, 4, 6, 8, 10, 12}, {10, 20, 30, 40, 50, 60}, 60, 30, 10, 10, 10, true, "pages: narrow reflow from the page start");
    check("1111222233334444", 4, {0, 6, 12}, {10, 20}, 20, 30, 10, 10, 10, true, "pages: wide reflow from the page start");

    const auto width = [](const std::string &line) { return static_cast<float>(line.size()); };
    const auto starts = storm::ui::WrapLines("aaa bb cccc\ndd", 6.0f, width);
    Expect(starts == std::vector<size_t>({0, 7, 12, 14}), "wrap: at spaces up to the width and at newlines");
    Expect(storm::ui::WrapLines("toolongword x", 4.0f, width) == std::vector<size_t>({0, 12, 13}), "wrap: a word wider than the line stays whole");
    Expect(storm::ui::WrapLines("a\n\nb", 9.0f, width) == std::vector<size_t>({0, 2, 3, 4}), "wrap: an empty line between two newlines");
    Expect(storm::ui::WrapLines("", 9.0f, width) == std::vector<size_t>({0}), "wrap: no text, no lines");
}

int main()
{
    Pages();
    const Json scope = Json::parse(R"({"label":"Купить <1>","n":1250000,"small":1250,"on":true,"off":false,"variant":"primary",
        "items":[{"label":"a","count":2},{"label":"b"}],"empty":[],"size":"md","spec":{"h":{"sm":40,"md":44}}})");

    ExpectText("{{label}}", scope, "Купить &lt;1&gt;");
    ExpectText("{{{label}}}", scope, "Купить <1>");
    ExpectText("{{ num(n) }} / {{ num(small) }}", scope, "1\xE2\x80\xAF" "250\xE2\x80\xAF" "000 / 1250");
    ExpectText("{{ on && !off ? 'y' : 'n' }}", scope, "y");
    ExpectText("{{ spec.h[size] + 4 }}", scope, "48");
    ExpectText("{{ missing }}|{{ or(missing, 'x') }}|{{ len(items) }}", scope, "|x|2");
    ExpectText("{{ variant == 'primary' ? 'ink' : 'duo' }}", scope, "ink");
    {
        // A call in a branch not taken is never made (tr of an empty key would be an error).
        std::vector<std::string> asked;
        storm::ui::SetTemplateStrings([&asked](const std::string &key) { asked.push_back(key); return "[" + key + "]"; });
        ExpectText("{{ missing ? tr(missing) : 'plain' }}|{{ variant ? tr('a.b') : tr(missing) }}", scope, "plain|[a.b]");
        Expect(asked == std::vector<std::string>({"a.b"}), "template: calls only in the branch taken");
        storm::ui::SetTemplateStrings(nullptr);
    }
    ExpectText("{{ pick(variant, 'danger', 1, 'primary', 2, 3) }}{{ in(size, 'sm', 'md') }}", scope, "2true");
    ExpectText("<t-if test=\"on\">A<t-else/>B</t-if><t-if test=\"off\">C<t-else/>D</t-if>", scope, "AD");
    ExpectText("<t-for each=\"items\" as=\"it\">[{{it_i}}:{{it.label}}<t-if test=\"it.count\">×{{it.count}}</t-if>{{ it_last ? '' : ',' }}]</t-for>", scope,
               "[0:a×2,][1:b]");
    ExpectText("<t-for each=\"empty\" as=\"it\">x</t-for>-", scope, "-");
    ExpectText("<t-if test=\"len(items) > 1 && items[1].label == 'b'\">ok</t-if>", scope, "ok");
    ExpectText("<div class=\"a {{ on ? 'on' : '' }}\">{{ 7 / 2 }} {{ 10 % 4 }} {{ -n < 0 }}</div>", scope, "<div class=\"a on\">3.5 2 true</div>");

    ExpectText("{{ replace('Удалить {s}?', '{s}', label) }}|{{ contains(variant, 'rim') }}|{{ ends('Цель?', '?') }}", scope,
               "Удалить Купить &lt;1&gt;?|true|true");
    ExpectText("<t-for each=\"range(int(split('2/3', '/')[1]))\" as=\"d\">{{ d < int(split('2/3', '/')[0]) ? '#' : '.' }}</t-for>", scope, "##.");
    const Json images = Json::parse(R"({"tok":{"icon":"ship","enamel":"red"},"pic":{"texture":"INTERFACES/items6","cell":3},
        "face":{"kind":"portrait","texture":"x"},"bare":{"icon":"anchor"}})");
    ExpectText("{{ image_kind(tok, '') }}|{{ image_kind(pic, '') }}|{{ image_kind(face, 'a') }}|{{ image_kind(bare, '') }}|"
               "{{ image_kind(none, 'anchor') }}|{{ image_kind(none, '') }}",
               images, "token|item|portrait|icon|icon|");
    ExpectText("{{ digits('1\xE2\x80\xAF" "500') + 1 }}", scope, "1501");

    // 2×2 A8R8G8B8: stored as B, G, R, A; output is premultiplied RGBA.
    {
        std::vector<uint8_t> data = TxHeader(2, 2, 21);
        const uint8_t pixels[16] = {10, 20, 30, 255, 0, 0, 255, 255, 255, 0, 0, 128, 1, 2, 3, 0};
        data.insert(data.end(), pixels, pixels + 16);
        storm::ui::Picture image;
        Expect(storm::ui::DecodeTx(data.data(), data.size(), image), "ARGB decode");
        Expect(image.width == 2 && image.height == 2, "ARGB size");
        Expect(image.rgba[0] == 30 && image.rgba[1] == 20 && image.rgba[2] == 10 && image.rgba[3] == 255, "ARGB channel order");
        Expect(image.rgba[8] == 0 && image.rgba[10] == 128 && image.rgba[11] == 128, "ARGB premultiplied");
        Expect(image.rgba[12] == 0 && image.rgba[15] == 0, "ARGB transparent");
    }
    // 1×1 R5G6B5 pure green, and A4R4G4B4 half-transparent red.
    {
        std::vector<uint8_t> data = TxHeader(1, 1, 23);
        data.push_back(0xE0);
        data.push_back(0x07);
        storm::ui::Picture image;
        Expect(storm::ui::DecodeTx(data.data(), data.size(), image) && image.rgba[0] == 0 && image.rgba[1] == 255 && image.rgba[2] == 0, "565 decode");
        data = TxHeader(1, 1, 26);
        data.push_back(0x00);
        data.push_back(0x8F);
        Expect(storm::ui::DecodeTx(data.data(), data.size(), image) && image.rgba[3] == 136 && image.rgba[0] == 136 && image.rgba[1] == 0, "4444 decode");
    }
    // 4×4 DXT1 block: colour0 red, colour1 blue, texel 0 = red, texel 1 = blue, rest 2/3 red.
    {
        std::vector<uint8_t> data = TxHeader(4, 4, 0x31545844);
        const uint8_t block[8] = {0x00, 0xF8, 0x1F, 0x00, 0xA4, 0xAA, 0xAA, 0xAA};
        data.insert(data.end(), block, block + 8);
        storm::ui::Picture image;
        Expect(storm::ui::DecodeTx(data.data(), data.size(), image), "DXT1 decode");
        Expect(image.rgba[0] == 255 && image.rgba[2] == 0, "DXT1 texel 0");
        Expect(image.rgba[4] == 0 && image.rgba[6] == 255, "DXT1 texel 1");
        Expect(image.rgba[8] == 170 && image.rgba[10] == 85, "DXT1 interpolated texel");
    }
    // Truncated and unknown data are refused.
    {
        std::vector<uint8_t> data = TxHeader(64, 64, 21);
        storm::ui::Picture image;
        Expect(!storm::ui::DecodeTx(data.data(), data.size(), image), "truncated refused");
        data = TxHeader(1, 1, 999);
        data.resize(40);
        Expect(!storm::ui::DecodeTx(data.data(), data.size(), image), "unknown format refused");
    }

    // No silent fallbacks: wrong specs, props, enum values, icons and expressions are errors.
    {
        const Json entry = Json::parse(R"({"tag":"c-x","props":{"variant":{"type":"enum","options":["a","b"],"default":"a"},
            "size":{"type":"enum","options":[44,48]},"label":{"type":"text"}}})");
        const auto spec = storm::ui::ParseComponentSpec(entry);
        Expect(spec->props.size() == 3, "spec props parsed");
        const auto &variant = *spec->FindAttribute("variant");
        Expect(storm::ui::CheckPropValue(variant, "b").empty(), "enum value accepted");
        Expect(!storm::ui::CheckPropValue(variant, "purple").empty(), "enum value outside options refused");
        Expect(storm::ui::CheckPropValue(*spec->FindAttribute("size"), "48").empty(), "numeric enum from attribute text accepted");
        Expect(storm::ui::CheckPropValue(*spec->FindAttribute("label"), "anything").empty(), "text prop takes anything");
        bool threw = false;
        try
        {
            storm::ui::ParseComponentSpec(Json::parse(R"({"tag":"c-y","props":{"p":{"type":"colour"}}})"));
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        Expect(threw, "unknown prop type refused");
        Expect(storm::ui::IsGenericAttribute("id") && storm::ui::IsGenericAttribute("data-action") && !storm::ui::IsGenericAttribute("colour"),
               "generic attributes");
        storm::ui::LoadIconPack(Json::parse(R"({"defs":"","symbols":{"i-ship":{"svg":"<symbol id=\"i-ship\"/>"},"c-rum":{"svg":"<symbol id=\"c-rum\"/>"}}})"));
        Expect(storm::ui::HasIcon("ship") && storm::ui::HasIcon("c-rum") && !storm::ui::HasIcon("anchor"), "icon lookup");
        float uv[4];
        Expect(storm::ui::AtlasCell(0, 4, uv) && uv[2] == 1.0f, "cell 0 is the whole texture");
        Expect(storm::ui::AtlasCell(6, 4, uv) && uv[0] == 0.25f && uv[1] == 0.25f && uv[2] == 0.5f, "atlas cell 6 of 4x4");
        Expect(!storm::ui::AtlasCell(17, 4, nullptr) && !storm::ui::AtlasCell(-1, 4, nullptr), "cell outside the atlas refused");
        ExpectText("{{ nosuchfunction(1) }}", scope, "<<template expression: unknown function nosuchfunction in ' nosuchfunction(1) '>>");
    }

    if (failures)
        return 1;
    std::printf("ui unit checks passed\n");
    return 0;
}
