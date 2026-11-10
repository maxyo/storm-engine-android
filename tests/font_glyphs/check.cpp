#include "font_glyphs.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using storm::font::GlyphTable;

namespace
{
// A synthetic fonts.ini: section -> key -> values in order.
using Ini = std::map<std::string, std::multimap<std::string, std::string>>;

GlyphTable read(const Ini &ini, const std::string &font)
{
    const auto values = [&](const std::string &section, const char *key) {
        std::vector<std::string> result;
        if (const auto s = ini.find(section); s != ini.end())
            for (auto [it, end] = s->second.equal_range(key); it != end; ++it)
                result.push_back(it->second);
        return result;
    };
    const auto hasFlag = [&](const std::string &section, const char *key) {
        const auto s = ini.find(section);
        return s != ini.end() && s->second.count(key) != 0;
    };
    return storm::font::readGlyphTable("fonts.ini", font, values, hasFlag);
}

std::string error(const Ini &ini, const std::string &font)
{
    try
    {
        read(ini, font);
    }
    catch (const std::runtime_error &e)
    {
        return e.what();
    }
    assert(!"expected an error");
    return {};
}
} // namespace

int main()
{
    // Escapes and UTF-8 glyphs.
    auto g = storm::font::parseGlyphEntry("com,1,2,3,4");
    assert(g.codepoint == ',' && g.x == 1 && g.y == 2 && g.width == 3 && g.height == 4);
    assert(storm::font::parseGlyphEntry("equ,0,0,1,1").codepoint == '=');
    assert(storm::font::parseGlyphEntry("q_,0,0,1,1").codepoint == 'q');
    assert(storm::font::parseGlyphEntry("Q,0,0,1,1").codepoint == 'Q');
    assert(storm::font::parseGlyphEntry("\xd0\x96, 5, 6, 7, 8").codepoint == 0x416); // Cyrillic Zhe
    assert(storm::font::parseGlyphEntry("\xe2\x82\xac,0,0,1,1").codepoint == 0x20ac); // Euro, past 0x2070
    g = storm::font::parseGlyphEntry("V,10,20,30,40,2,7");
    assert(g.kernLeft == 2 && g.kernRight == 7);

    // Own table, template inheritance, TEHO fallback.
    Ini ini;
    ini["BASE"] = {{"char", "A,0,0,10,20"}, {"char", "b_,10,0,8,20"}, {"GroupTemplate", "BASE"}};
    ini["TINTED"] = {{"Texture", "other.tga"}, {"GroupTemplate", "BASE"}};
    ini["OLD"] = {{"char_A", "0,0,10,20"}};
    auto own = read(ini, "BASE");
    assert(own.section == "BASE" && own.glyphs.size() == 2 && own.glyphs[1].codepoint == 'b');
    auto inherited = read(ini, "TINTED");
    assert(inherited.section == "BASE" && inherited.glyphs.size() == 2 && inherited.glyphs[0].width == 10);
    assert(read(ini, "OLD").glyphs.empty());

    // Kerning: only with the flag; then the glyph overhangs and the pen advances less.
    ini["ITALIC"] = {{"Kerning", ""}, {"char", "F,0,0,40,70,1,20"}};
    ini["TILTED"] = {{"Kerning", ""}, {"GroupTemplate", "ITALIC"}};
    ini["PLAIN"] = {{"GroupTemplate", "ITALIC"}};
    auto italic = read(ini, "TILTED");
    assert(italic.section == "ITALIC" && italic.glyphs[0].kernRight == 20);
    auto geometry = storm::font::glyphGeometry(italic.glyphs[0], 256, 128, 70, 0.5f, 0.5f);
    assert(geometry.x1 == -0.5f && geometry.x2 == 19.5f && geometry.advanceReduction == 10.5f);
    assert(error(ini, "PLAIN").find("kerning values in a font without the Kerning flag") != std::string::npos);
    assert(error(ini, "PLAIN").find("(template of PLAIN)") != std::string::npos);

    // Without kerning the geometry is the TEHO arithmetic.
    geometry = storm::font::glyphGeometry(own.glyphs[0], 256, 128, 15, 0.75f, 0.75f);
    assert(geometry.x1 == 0.f && geometry.x2 == 7.f && geometry.advanceReduction == 0.f);
    assert(geometry.y1 == 0.f && geometry.y2 == 15.f);
    assert(geometry.u1 == 0.5f / 256 && geometry.v2 == 0.5f / 128 + 19.f / 128);

    // Errors name file, section and entry.
    ini["BROKEN"] = {{"char", "A,0,0,10,20"}, {"char", "B,0,0,10"}};
    assert(error(ini, "BROKEN") ==
           "fonts.ini [BROKEN]: char entry 2 'B,0,0,10': want glyph,x,y,w,h or glyph,x,y,w,h,kernLeft,kernRight");
    ini["ESCAPE"] = {{"char", "dot,0,0,1,1"}};
    assert(error(ini, "ESCAPE").find("unknown glyph 'dot'") != std::string::npos);
    ini["NUMBER"] = {{"char", "A,0,x,1,1"}};
    assert(error(ini, "NUMBER").find("field 3 'x' is not a number") != std::string::npos);
    ini["ORPHAN"] = {{"GroupTemplate", "NOWHERE"}};
    assert(error(ini, "ORPHAN") == "fonts.ini [ORPHAN]: GroupTemplate NOWHERE has no char entries");

    std::cout << "font glyphs: escapes, UTF-8, templates, kerning, TEHO fallback and errors PASS\n";
}
