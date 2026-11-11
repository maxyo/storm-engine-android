#pragma once

// Glyph tables of the bitmap fonts (fonts.ini). Two formats exist; FONT::Init picks per section:
// - To Each His Own: one key per glyph, `char_<glyph> = x,y,w,h` (read by FONT::Init itself);
// - Caribbean Legend: repeated `char = <glyph>,x,y,w,h[,kernLeft,kernRight]`. <glyph> is one UTF-8
//   character or one of the escapes kept from the TEHO key names: `com` = ',', `equ` = '=',
//   `<a..z>_` = that lowercase letter. A section without
//   own `char` entries takes the table of the section named by `GroupTemplate` (its own texture,
//   height and scale still apply). The two kerning numbers are only allowed in sections with the
//   `Kerning` flag; read from the data (largest right values on italic F, T, P), they are the
//   pixels the glyph overhangs its neighbours: it is drawn `kernLeft` to the left of the pen and
//   the pen advances `w - kernLeft - kernRight`.
// Malformed entries, unknown escapes and missing templates are errors naming font and entry.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::font
{
struct GlyphEntry
{
    uint32_t codepoint = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t kernLeft = 0;
    int32_t kernRight = 0;
};

struct GlyphTable
{
    std::string section; // where the entries come from: the font itself or its GroupTemplate
    std::vector<GlyphEntry> glyphs;
};

// `values(section, key)` returns every value of a key in a section of the font file, in order;
// `hasFlag(section, key)` whether a key is present (with or without a value).
// Returns an empty table when the font has no `char` entries and no template (the TEHO format).
GlyphTable readGlyphTable(std::string_view file, std::string_view font,
                          const std::function<std::vector<std::string>(const std::string &, const char *)> &values,
                          const std::function<bool(const std::string &, const char *)> &hasFlag);

// Parses one `char` value; throws std::invalid_argument with the reason.
GlyphEntry parseGlyphEntry(std::string_view value);

// Screen and texture rectangles of a glyph, as FONT::Init builds them for the TEHO format.
struct GlyphGeometry
{
    float x1, y1, x2, y2;     // position relative to the pen and the line top
    float u1, v1, u2, v2;     // texture coordinates
    float advanceReduction;   // subtracted from x2 - x1 when the pen advances
};

GlyphGeometry glyphGeometry(const GlyphEntry &glyph, int32_t textureWidth, int32_t textureHeight, int32_t lineHeight,
                            float horizontalAspect, float verticalAspect);
} // namespace storm::font
