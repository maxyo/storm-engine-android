#include "font_glyphs.hpp"

#include "utf8.h"

#include <charconv>
#include <stdexcept>

namespace storm::font
{
namespace
{
std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

uint32_t glyphCodepoint(std::string_view token)
{
    if (token == "com")
        return ',';
    if (token == "equ")
        return '=';
    if (token.size() == 2 && token[1] == '_' && token[0] >= 'a' && token[0] <= 'z')
        return static_cast<uint32_t>(token[0]);
    const std::string text(token);
    if (!text.empty() && static_cast<size_t>(utf8::u8_inc(text.c_str())) == text.size())
    {
        const int codepoint = utf8::Utf8ToCodepoint(text.c_str());
        if (codepoint >= 0)
            return static_cast<uint32_t>(codepoint);
    }
    throw std::invalid_argument("unknown glyph '" + text + "' (one UTF-8 character, com, equ or <a..z>_)");
}
} // namespace

GlyphEntry parseGlyphEntry(std::string_view value)
{
    std::vector<std::string_view> fields;
    for (size_t start = 0;;)
    {
        const size_t comma = value.find(',', start);
        fields.push_back(trim(value.substr(start, comma == std::string_view::npos ? comma : comma - start)));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    if (fields.size() != 5 && fields.size() != 7)
        throw std::invalid_argument("want glyph,x,y,w,h or glyph,x,y,w,h,kernLeft,kernRight");

    GlyphEntry glyph;
    glyph.codepoint = glyphCodepoint(fields[0]);
    int32_t *numbers[] = {&glyph.x, &glyph.y, &glyph.width, &glyph.height, &glyph.kernLeft, &glyph.kernRight};
    for (size_t i = 1; i < fields.size(); i++)
    {
        const auto field = fields[i];
        const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), *numbers[i - 1]);
        if (error != std::errc{} || end != field.data() + field.size())
            throw std::invalid_argument("field " + std::to_string(i + 1) + " '" + std::string(field) +
                                        "' is not a number");
    }
    if (glyph.width <= 0 || glyph.height <= 0)
        throw std::invalid_argument("glyph size must be positive");
    return glyph;
}

GlyphTable readGlyphTable(std::string_view file, std::string_view font,
                          const std::function<std::vector<std::string>(const std::string &, const char *)> &values,
                          const std::function<bool(const std::string &, const char *)> &hasFlag)
{
    const std::string fontName(font);
    const auto where = [&](const std::string &section) {
        return std::string(file) + " [" + section + "]";
    };

    GlyphTable table{fontName, {}};
    auto entries = values(fontName, "char");
    if (entries.empty())
    {
        const auto templates = values(fontName, "GroupTemplate");
        if (templates.empty())
            return table; // TEHO format: char_<glyph> keys
        table.section = std::string(trim(templates.front()));
        entries = values(table.section, "char");
        if (entries.empty())
            throw std::runtime_error(where(fontName) + ": GroupTemplate " + table.section + " has no char entries");
    }

    const bool kerning = hasFlag(fontName, "Kerning");
    for (size_t i = 0; i < entries.size(); i++)
    {
        GlyphEntry glyph;
        try
        {
            glyph = parseGlyphEntry(entries[i]);
            if (!kerning && (glyph.kernLeft != 0 || glyph.kernRight != 0))
                throw std::invalid_argument("kerning values in a font without the Kerning flag");
        }
        catch (const std::invalid_argument &error)
        {
            throw std::runtime_error(where(table.section) + ": char entry " + std::to_string(i + 1) + " '" +
                                     entries[i] + "': " + error.what() +
                                     (table.section != fontName ? " (template of " + fontName + ")" : ""));
        }
        table.glyphs.push_back(glyph);
    }
    return table;
}

GlyphGeometry glyphGeometry(const GlyphEntry &glyph, int32_t textureWidth, int32_t textureHeight, int32_t lineHeight,
                            float horizontalAspect, float verticalAspect)
{
    GlyphGeometry g{};
    // The same arithmetic as the TEHO path of FONT::Init, shifted left by the left kerning.
    g.x1 = -glyph.kernLeft * horizontalAspect;
    g.x2 = g.x1 + static_cast<float>(static_cast<int32_t>(glyph.width * horizontalAspect));
    g.u1 = static_cast<float>(glyph.x + .5f) / static_cast<float>(textureWidth);
    g.u2 = g.u1 + static_cast<float>(glyph.width - 1.f) / static_cast<float>(textureWidth);
    g.v1 = static_cast<float>(glyph.y + .5f) / static_cast<float>(textureHeight);
    g.v2 = g.v1 + static_cast<float>(glyph.height - 1.f) / static_cast<float>(textureHeight);
    g.y1 = static_cast<float>(lineHeight - static_cast<int32_t>(glyph.height * verticalAspect));
    g.y2 = static_cast<float>(lineHeight);
    g.advanceReduction = (glyph.kernLeft + glyph.kernRight) * horizontalAspect;
    return g;
}
} // namespace storm::font
