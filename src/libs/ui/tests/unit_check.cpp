// Host checks for the parts of the UI runtime that need no GL context:
// the template language and the .tx decoder (synthetic data only).
#include "../src/elements.h"
#include "../src/template.h"

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

int main()
{
    const Json scope = Json::parse(R"({"label":"Купить <1>","n":1250000,"small":1250,"on":true,"off":false,"variant":"primary",
        "items":[{"label":"a","count":2},{"label":"b"}],"empty":[],"size":"md","spec":{"h":{"sm":40,"md":44}}})");

    ExpectText("{{label}}", scope, "Купить &lt;1&gt;");
    ExpectText("{{{label}}}", scope, "Купить <1>");
    ExpectText("{{ num(n) }} / {{ num(small) }}", scope, "1\xE2\x80\xAF" "250\xE2\x80\xAF" "000 / 1250");
    ExpectText("{{ on && !off ? 'y' : 'n' }}", scope, "y");
    ExpectText("{{ spec.h[size] + 4 }}", scope, "48");
    ExpectText("{{ missing }}|{{ or(missing, 'x') }}|{{ len(items) }}", scope, "|x|2");
    ExpectText("{{ variant == 'primary' ? 'ink' : 'duo' }}", scope, "ink");
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
    ExpectText("{{ digits('1\xE2\x80\xAF" "500') + 1 }}", scope, "1501");

    // 2×2 A8R8G8B8: stored as B, G, R, A; output is premultiplied RGBA.
    {
        std::vector<uint8_t> data = TxHeader(2, 2, 21);
        const uint8_t pixels[16] = {10, 20, 30, 255, 0, 0, 255, 255, 255, 0, 0, 128, 1, 2, 3, 0};
        data.insert(data.end(), pixels, pixels + 16);
        storm::ui::TxImage image;
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
        storm::ui::TxImage image;
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
        storm::ui::TxImage image;
        Expect(storm::ui::DecodeTx(data.data(), data.size(), image), "DXT1 decode");
        Expect(image.rgba[0] == 255 && image.rgba[2] == 0, "DXT1 texel 0");
        Expect(image.rgba[4] == 0 && image.rgba[6] == 255, "DXT1 texel 1");
        Expect(image.rgba[8] == 170 && image.rgba[10] == 85, "DXT1 interpolated texel");
    }
    // Truncated and unknown data are refused.
    {
        std::vector<uint8_t> data = TxHeader(64, 64, 21);
        storm::ui::TxImage image;
        Expect(!storm::ui::DecodeTx(data.data(), data.size(), image), "truncated refused");
        data = TxHeader(1, 1, 999);
        data.resize(40);
        Expect(!storm::ui::DecodeTx(data.data(), data.size(), image), "unknown format refused");
    }

    if (failures)
        return 1;
    std::printf("ui unit checks passed\n");
    return 0;
}
