#include "dlg_dsl.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace storm::dlg
{
namespace
{
// U+21B4 RIGHTWARDS ARROW WITH CORNER DOWNWARDS, the lower-case marker.
constexpr std::string_view kLowerMarker = "\xE2\x86\xB4";

bool isIdentStart(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(const char c)
{
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

char asciiLower(const char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isBlank(const char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Length of the UTF-8 sequence starting with `lead` (1 for ASCII and stray bytes).
size_t utf8Length(const unsigned char lead)
{
    if (lead >= 0xF0 && lead <= 0xF7)
        return 4;
    if (lead >= 0xE0)
        return lead <= 0xEF ? 3 : 1;
    if (lead >= 0xC0)
        return 2;
    return 1;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && isBlank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isBlank(s.back()))
        s.remove_suffix(1);
    return s;
}

// --- numbers ------------------------------------------------------------------------------

// Locale-independent decimal parser: [+-]digits[.digits][(e|E)[+-]digits], blanks around.
std::optional<double> parseNumber(std::string_view s)
{
    s = trim(s);
    if (s.empty())
        return std::nullopt;
    size_t i = 0;
    bool negative = false;
    if (s[i] == '+' || s[i] == '-')
        negative = s[i++] == '-';
    double value = 0;
    bool digits = false;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, digits = true)
        value = value * 10 + (s[i] - '0');
    if (i < s.size() && s[i] == '.')
    {
        double scale = 0.1;
        for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, digits = true, scale /= 10)
            value += (s[i] - '0') * scale;
    }
    if (!digits)
        return std::nullopt;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
    {
        ++i;
        bool negativeExponent = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            negativeExponent = s[i++] == '-';
        int exponent = 0;
        bool exponentDigits = false;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, exponentDigits = true)
            exponent = std::min(exponent * 10 + (s[i] - '0'), 400);
        if (!exponentDigits)
            return std::nullopt;
        value *= std::pow(10.0, negativeExponent ? -exponent : exponent);
    }
    if (i != s.size())
        return std::nullopt;
    return negative ? -value : value;
}

// At most two decimals, no trailing zeros: 20 -> "20", 20.5 -> "20.5", 0.376 -> "0.38".
std::string humanNumber(const double value)
{
    const long long hundredths = std::llround(value * 100);
    const bool negative = hundredths < 0;
    const unsigned long long magnitude = negative ? 0ULL - static_cast<unsigned long long>(hundredths)
                                                  : static_cast<unsigned long long>(hundredths);
    std::string result = negative ? "-" : "";
    result += std::to_string(magnitude / 100);
    const auto fraction = static_cast<unsigned>(magnitude % 100);
    if (fraction != 0)
    {
        result += '.';
        result += static_cast<char>('0' + fraction / 10);
        if (fraction % 10 != 0)
            result += static_cast<char>('0' + fraction % 10);
    }
    return result;
}

// Slavic plural rule used by CL's declension helpers: 1, 21 -> one; 2-4, 22-24 -> few;
// 0, 5-20, 11-14 -> many (falls back to `few` when no `many` form is given).
std::string_view declensionForm(long long n, std::string_view one, std::string_view few, std::string_view many)
{
    if (many.empty())
        many = few;
    n = n < 0 ? -n : n;
    if (const long long rest = n % 100; rest >= 11 && rest <= 14)
        return many;
    switch (n % 10)
    {
    case 1:
        return one;
    case 2:
    case 3:
    case 4:
        return few;
    default:
        return many;
    }
}

// --- built-ins ----------------------------------------------------------------------------

enum class Builtin
{
    Name,
    LastName,
    FullName,
    ShipName,
    NpcName,
    NpcLastName,
    NpcFullName,
    NpcShipName,
    City,
    Sir,
    NpcSir,
    Swear,
    Hello,
    Bye,
    Br,
    Red,
    DialogBreak,
    Ctx,
    Attr,
    CtxHasAttr,
    Rand,
    ManWoman,
    NpcManWoman,
    CtxManWoman,
    GoodBad,
    Key,
    Link,
    Value,
    Percent,
    Number,
    Money,
    Abs,
    Days,
    Decl,
    NationI,
    GoodI,
    ModifierPercent,
};

struct BuiltinName
{
    std::string_view name; // lower case
    Builtin builtin;
};

constexpr std::array kBuiltins{
    BuiltinName{"name", Builtin::Name},
    BuiltinName{"lastname", Builtin::LastName},
    BuiltinName{"fullname", Builtin::FullName},
    BuiltinName{"shipname", Builtin::ShipName},
    BuiltinName{"npcname", Builtin::NpcName},
    BuiltinName{"npclastname", Builtin::NpcLastName},
    BuiltinName{"npcfullname", Builtin::NpcFullName},
    BuiltinName{"npcshipname", Builtin::NpcShipName},
    BuiltinName{"city", Builtin::City},
    BuiltinName{"sir", Builtin::Sir},
    BuiltinName{"npcsir", Builtin::NpcSir},
    BuiltinName{"swear", Builtin::Swear},
    BuiltinName{"hello", Builtin::Hello},
    BuiltinName{"bye", Builtin::Bye},
    BuiltinName{"br", Builtin::Br},
    BuiltinName{"red", Builtin::Red},
    BuiltinName{"dialogbreak", Builtin::DialogBreak},
    BuiltinName{"ctx", Builtin::Ctx},
    BuiltinName{"attr", Builtin::Attr},
    BuiltinName{"ctxhasattr", Builtin::CtxHasAttr},
    BuiltinName{"rand", Builtin::Rand},
    BuiltinName{"manwoman", Builtin::ManWoman},
    BuiltinName{"npcmanwoman", Builtin::NpcManWoman},
    BuiltinName{"ctxmanwoman", Builtin::CtxManWoman},
    BuiltinName{"goodbad", Builtin::GoodBad},
    BuiltinName{"key", Builtin::Key},
    BuiltinName{"link", Builtin::Link},
    BuiltinName{"value", Builtin::Value},
    BuiltinName{"percent", Builtin::Percent},
    BuiltinName{"number", Builtin::Number},
    BuiltinName{"money", Builtin::Money},
    BuiltinName{"abs", Builtin::Abs},
    BuiltinName{"days", Builtin::Days},
    BuiltinName{"decl", Builtin::Decl},
    BuiltinName{"nationi", Builtin::NationI},
    BuiltinName{"goodi", Builtin::GoodI},
    BuiltinName{"modifierpercent", Builtin::ModifierPercent},
};

std::optional<Builtin> findBuiltin(std::string_view name)
{
    constexpr size_t kLongest = 16;
    if (name.size() > kLongest)
        return std::nullopt;
    std::array<char, kLongest> lower{};
    std::transform(name.begin(), name.end(), lower.begin(), asciiLower);
    const std::string_view key(lower.data(), name.size());
    for (const auto &entry : kBuiltins)
        if (entry.name == key)
            return entry.builtin;
    return std::nullopt;
}

ScriptArg text(std::string value)
{
    return ScriptArg{ScriptArg::Kind::Text, std::move(value)};
}

ScriptArg ref(const ScriptArg::Kind kind)
{
    return ScriptArg{kind, {}};
}

// Lower-cases one code point in place; returns the byte length consumed at `pos`, or 0
// when the code point is not a letter (nothing changed). Covers ASCII, Latin-1, Latin
// Extended-A, Greek and Cyrillic upper case.
size_t lowerCodePoint(std::string &s, const size_t pos, bool &isLetter)
{
    const auto c0 = static_cast<unsigned char>(s[pos]);
    const size_t length = std::min(utf8Length(c0), s.size() - pos);
    isLetter = false;
    if (length == 1)
    {
        if (c0 >= 'A' && c0 <= 'Z')
            s[pos] = static_cast<char>(c0 - 'A' + 'a');
        isLetter = (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z');
        return 1;
    }
    if (length != 2)
    {
        // Three/four-byte sequences: scripts without case (CJK, Hangul, kana) count as
        // letters; punctuation and symbols (U+2000..U+2E7F, the marker itself) do not.
        if (length == 3 && pos + 2 < s.size())
        {
            const unsigned cp = ((c0 & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[pos + 1]) & 0x3Fu) << 6) |
                                (static_cast<unsigned char>(s[pos + 2]) & 0x3Fu);
            isLetter = cp >= 0x2E80 || (cp >= 0x1100 && cp < 0x2000);
        }
        else
            isLetter = length == 4;
        return length;
    }
    const auto c1 = static_cast<unsigned char>(s[pos + 1]);
    unsigned cp = ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
    if (cp < 0xC0)
        return 2;
    isLetter = true;
    unsigned lower = cp;
    if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) || (cp >= 0x391 && cp <= 0x3AB && cp != 0x3A2) ||
        (cp >= 0x410 && cp <= 0x42F))
        lower = cp + 0x20;
    else if (cp >= 0x400 && cp <= 0x40F)
        lower = cp + 0x50;
    else if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x138 && cp != 0x149 && cp != 0x17F)
    {
        const bool oddRun = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
        if (cp == 0x178)
            lower = 0xFF;
        else if (oddRun ? (cp % 2 == 1) : (cp % 2 == 0))
            lower = cp + 1;
    }
    else if (cp == 0x130) // Latin capital I with dot above -> i
    {
        s.replace(pos, 2, "i");
        return 1;
    }
    if (lower != cp)
    {
        s[pos] = static_cast<char>(0xC0 | (lower >> 6));
        s[pos + 1] = static_cast<char>(0x80 | (lower & 0x3F));
    }
    return 2;
}

} // namespace

std::string_view diagnosticKindName(const Diagnostic::Kind kind)
{
    switch (kind)
    {
    case Diagnostic::Kind::UnknownName:
        return "unknown name";
    case Diagnostic::Kind::Unterminated:
        return "unterminated call";
    case Diagnostic::Kind::MissingKey:
        return "missing key";
    case Diagnostic::Kind::MissingContext:
        return "missing context attribute";
    case Diagnostic::Kind::MissingCharacter:
        return "missing character";
    case Diagnostic::Kind::BadArgument:
        return "bad argument";
    case Diagnostic::Kind::RecursionLimit:
        return "recursion limit";
    case Diagnostic::Kind::Placeholder:
        return "placeholder";
    }
    return "?";
}

bool lowerFirstLetter(std::string &text, size_t from)
{
    size_t pos = from;
    while (pos < text.size())
    {
        const char c = text[pos];
        if (isBlank(c))
        {
            ++pos;
            continue;
        }
        if (c == '[')
        {
            if (const size_t close = text.find(']', pos); close != std::string::npos)
            {
                pos = close + 1;
                continue;
            }
        }
        if (c >= '0' && c <= '9')
            return true;
        bool isLetter = false;
        const size_t length = lowerCodePoint(text, pos, isLetter);
        if (isLetter)
            return true;
        pos += length;
    }
    return false;
}

void replaceAllMatches(std::string &input, const std::string_view key, const std::string_view replacement, size_t from)
{
    if (key.empty() || from >= input.size())
        return;
    std::string result;
    size_t copied = 0;
    for (size_t found = input.find(key, from); found != std::string::npos; found = input.find(key, found + key.size()))
    {
        result.append(input, copied, found - copied);
        result.append(replacement);
        copied = found + key.size();
    }
    if (copied == 0)
        return;
    result.append(input, copied, std::string::npos);
    input = std::move(result);
}

// --- expander -----------------------------------------------------------------------------

struct Expander::Call
{
    std::string_view name;
    std::string_view raw;               // the whole construct as written
    std::vector<std::string_view> args; // raw (unexpanded) arguments
    bool isCall = false;                // `$name(...)` rather than `$name`
};

Expander::Expander(Host &host) : host_(host)
{
}

void Expander::report(const Diagnostic::Kind kind, const std::string_view name, std::string detail)
{
    std::string key(diagnosticKindName(kind));
    key += '\0';
    key += name;
    if (reported_.insert(std::move(key)).second)
        host_.report(Diagnostic{kind, std::string(name), std::move(detail)});
}

std::string Expander::expand(const std::string_view text)
{
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    expandInto(out, text, 0);
    return out;
}

std::string Expander::expandArg(const std::string_view text, const int depth)
{
    std::string out;
    out.reserve(text.size());
    expandInto(out, text, depth);
    return out;
}

std::string Expander::expandValue(const std::string_view value, const std::string_view name, const int depth)
{
    if (depth + 1 >= kMaxDepth)
    {
        report(Diagnostic::Kind::RecursionLimit, name, "nested expansion deeper than " + std::to_string(kMaxDepth));
        return std::string(value);
    }
    return expandArg(value, depth + 1);
}

std::string Expander::expandInFile(const std::string_view value, const std::string_view file, const int depth)
{
    std::string saved = std::move(currentFile_);
    currentFile_ = file;
    std::string out;
    out.reserve(value.size() + value.size() / 4);
    expandInto(out, value, depth);
    currentFile_ = std::move(saved);
    return out;
}

void Expander::expandInto(std::string &out, const std::string_view text, const int depth)
{
    std::optional<size_t> pendingLower;
    size_t pos = 0;
    while (pos < text.size())
    {
        const size_t unitStart = out.size();
        const char c = text[pos];
        if (c == '$')
        {
            pos = expandConstruct(out, text, pos, depth);
        }
        else if (c == '\\')
        {
            if (pos + 1 >= text.size())
            {
                out += '\\';
                ++pos;
            }
            else if (text[pos + 1] == 'n')
            {
                out.append("\\n");
                pos += 2;
            }
            else
            {
                const size_t length = std::min(utf8Length(static_cast<unsigned char>(text[pos + 1])), text.size() - pos - 1);
                out.append(text.substr(pos + 1, length));
                pos += 1 + length;
            }
        }
        else if (text.compare(pos, kLowerMarker.size(), kLowerMarker) == 0)
        {
            pendingLower = out.size();
            pos += kLowerMarker.size();
            continue;
        }
        else
        {
            size_t end = pos + 1;
            while (end < text.size() && text[end] != '$' && text[end] != '\\' && text[end] != kLowerMarker[0])
                ++end;
            out.append(text.substr(pos, end - pos));
            pos = end;
        }
        if (pendingLower && out.size() > unitStart && lowerFirstLetter(out, *pendingLower))
            pendingLower.reset();
    }
}

size_t Expander::expandConstruct(std::string &out, const std::string_view text, const size_t start, const int depth)
{
    size_t pos = start + 1;
    if (pos >= text.size() || !isIdentStart(text[pos]))
    {
        out += '$';
        return pos;
    }
    while (pos < text.size() && isIdentChar(text[pos]))
        ++pos;
    Call call;
    call.name = text.substr(start + 1, pos - start - 1);
    if (pos < text.size() && text[pos] == '(')
    {
        call.isCall = true;
        // Split the arguments at top-level `|`, skipping escapes and nested calls.
        size_t argStart = pos + 1;
        int nested = 0;
        size_t i = argStart;
        bool closed = false;
        while (i < text.size())
        {
            const char c = text[i];
            if (c == '\\')
            {
                i += 2;
                continue;
            }
            if (c == '$')
            {
                size_t j = i + 1;
                if (j < text.size() && isIdentStart(text[j]))
                {
                    while (j < text.size() && isIdentChar(text[j]))
                        ++j;
                    if (j < text.size() && text[j] == '(')
                        ++nested;
                    i = j + (j < text.size() && text[j] == '(' ? 1 : 0);
                    continue;
                }
            }
            else if (c == '|' && nested == 0)
            {
                call.args.push_back(text.substr(argStart, i - argStart));
                argStart = i + 1;
            }
            else if (c == ')')
            {
                if (nested == 0)
                {
                    closed = true;
                    break;
                }
                --nested;
            }
            ++i;
        }
        if (!closed)
        {
            report(Diagnostic::Kind::Unterminated, call.name, "no closing ')' for $" + std::string(call.name) + "(");
            out.append(text.substr(start));
            return text.size();
        }
        if (i > pos + 1 || !call.args.empty())
            call.args.push_back(text.substr(argStart, i - argStart));
        pos = i + 1;
    }
    call.raw = text.substr(start, pos - start);
    if (!builtin(out, call, depth) && !scriptFallback(out, call, depth))
        unknown(out, call);
    return pos;
}

void Expander::unknown(std::string &out, const Call &call)
{
    report(Diagnostic::Kind::UnknownName, call.name,
           "no built-in or script function (tried name, DLG_, RT_, capitalised) for " + std::string(call.raw));
    out.append(call.raw);
}

bool Expander::scriptFallback(std::string &out, const Call &call, const int depth)
{
    std::vector<ScriptArg> args;
    args.reserve(call.args.size());
    for (const auto arg : call.args)
        args.push_back(text(expandArg(arg, depth)));
    std::string capitalised(call.name);
    capitalised[0] = static_cast<char>(capitalised[0] >= 'a' && capitalised[0] <= 'z' ? capitalised[0] - 'a' + 'A'
                                                                                     : capitalised[0]);
    const std::array<std::string, 4> candidates{std::string(call.name), "DLG_" + capitalised, "RT_" + capitalised,
                                                capitalised};
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i == 3 && capitalised == call.name)
            break;
        if (auto result = host_.callScript(candidates[i], args))
        {
            out.append(*result);
            return true;
        }
    }
    return false;
}

bool Expander::builtin(std::string &out, const Call &call, const int depth)
{
    const auto found = findBuiltin(call.name);
    if (!found)
        return false;
    const std::string name(call.name);
    const auto &raw = call.args;
    const size_t argc = raw.size();

    const auto fail = [&](const Diagnostic::Kind kind, std::string detail) {
        report(kind, name, std::move(detail));
        out.append(call.raw);
        return true;
    };
    const auto needArgs = [&](const size_t low, const size_t high) {
        if (argc >= low && argc <= high)
            return true;
        const std::string range = high == SIZE_MAX ? "at least " + std::to_string(low)
                                  : high == low    ? std::to_string(low)
                                                   : std::to_string(low) + ".." + std::to_string(high);
        fail(Diagnostic::Kind::BadArgument,
             "$" + name + " takes " + range + " argument(s), got " + std::to_string(argc) + ": " + std::string(call.raw));
        return false;
    };
    const auto arg = [&](const size_t i) { return expandArg(raw[i], depth); };
    const auto field = [&](const Who who, const CharacterField what) {
        if (!needArgs(0, 0))
            return true;
        if (auto value = host_.characterField(who, what))
        {
            out.append(*value);
            return true;
        }
        return fail(Diagnostic::Kind::MissingCharacter, "character property unavailable for " + std::string(call.raw));
    };
    const auto script = [&](std::string_view function, std::vector<ScriptArg> args) {
        if (auto value = host_.callScript(function, args))
        {
            out.append(*value);
            return true;
        }
        return fail(Diagnostic::Kind::UnknownName, "$" + name + " needs script function " + std::string(function));
    };
    const auto bySex = [&](const Who who) {
        if (!needArgs(2, 2))
            return true;
        const auto female = host_.isFemale(who);
        if (!female)
            return fail(Diagnostic::Kind::MissingCharacter, "no character for " + std::string(call.raw));
        expandInto(out, raw[*female ? 1 : 0], depth);
        return true;
    };
    const auto number = [&](std::string_view what) -> std::optional<double> {
        const std::string value = arg(0);
        if (auto parsed = parseNumber(value))
            return parsed;
        fail(Diagnostic::Kind::BadArgument, "$" + name + " needs a number, got '" + value + "' in " + std::string(what));
        return std::nullopt;
    };

    switch (*found)
    {
    case Builtin::Name:
        return field(Who::Player, CharacterField::FirstName);
    case Builtin::LastName:
        return field(Who::Player, CharacterField::LastName);
    case Builtin::FullName:
        return field(Who::Player, CharacterField::FullName);
    case Builtin::ShipName:
        return field(Who::Player, CharacterField::ShipName);
    case Builtin::NpcName:
        return field(Who::Npc, CharacterField::FirstName);
    case Builtin::NpcLastName:
        return field(Who::Npc, CharacterField::LastName);
    case Builtin::NpcFullName:
        return field(Who::Npc, CharacterField::FullName);
    case Builtin::NpcShipName:
        return field(Who::Npc, CharacterField::ShipName);
    case Builtin::City:
        return field(Who::Npc, CharacterField::Town);
    case Builtin::Sir:
        return !needArgs(0, 0) || script("GetAddress_Form", {ref(ScriptArg::Kind::Npc)});
    case Builtin::NpcSir:
        return !needArgs(0, 0) || script("GetAddress_FormToNPC", {ref(ScriptArg::Kind::Npc)});
    case Builtin::Swear:
        return !needArgs(0, 0) || script("RandSwear", {});
    case Builtin::Hello:
        return !needArgs(0, 0) || script("TimeGreeting", {});
    case Builtin::Bye:
        return !needArgs(0, 0) || script("Goodbye", {text("1")});
    case Builtin::Br:
        if (needArgs(0, 0))
            out.append(host_.markup(Markup::LineBreak));
        return true;
    case Builtin::Red:
        if (needArgs(0, 0))
            out.append(host_.markup(Markup::Indent));
        return true;
    case Builtin::DialogBreak:
        if (needArgs(0, 0))
            out.append(host_.markup(Markup::DialogBreak));
        return true;
    case Builtin::Ctx: {
        if (!needArgs(1, 1))
            return true;
        const std::string path = arg(0);
        const auto value = host_.contextAttribute(path);
        if (!value)
            return fail(Diagnostic::Kind::MissingContext, "no context attribute '" + path + "'");
        out.append(expandValue(*value, name, depth));
        return true;
    }
    case Builtin::Attr: {
        if (!needArgs(1, 1))
            return true;
        constexpr std::string_view prefix = "$context.";
        const std::string_view path = raw[0];
        if (path.substr(0, prefix.size()) != prefix || path.size() == prefix.size())
            return fail(Diagnostic::Kind::BadArgument, "$attr expects $context.<path>: " + std::string(call.raw));
        const auto value = host_.contextAttribute(path.substr(prefix.size()));
        if (!value)
            return fail(Diagnostic::Kind::MissingContext,
                        "no context attribute '" + std::string(path.substr(prefix.size())) + "'");
        out.append(*value);
        return true;
    }
    case Builtin::CtxHasAttr: {
        if (!needArgs(2, 3))
            return true;
        if (host_.contextAttribute(arg(0)))
            expandInto(out, raw[1], depth);
        else if (argc == 3)
            expandInto(out, raw[2], depth);
        return true;
    }
    case Builtin::Rand:
        if (needArgs(1, SIZE_MAX))
        {
            const int choice = host_.randomInt(static_cast<int>(argc));
            expandInto(out, raw[static_cast<size_t>(std::clamp(choice, 0, static_cast<int>(argc) - 1))], depth);
        }
        return true;
    case Builtin::ManWoman:
        return bySex(Who::Player);
    case Builtin::NpcManWoman:
        return bySex(Who::Npc);
    case Builtin::CtxManWoman:
        return bySex(Who::Context);
    case Builtin::GoodBad:
        if (needArgs(3, 3))
            expandInto(out, raw[static_cast<size_t>(std::clamp(host_.reputationTier(), 0, 2))], depth);
        return true;
    case Builtin::Key: {
        if (!needArgs(1, 2))
            return true;
        // $key(k): common strings, then the current file (Common_Business.txt refers to its own
        // keys; personality.txt `Boatswain {$key(Boatswain)}` needs common first).
        const std::string key = arg(0);
        std::string file = argc == 2 ? arg(1) : std::string();
        auto value = host_.lookupKey(file, key);
        if (!value && argc == 1 && !currentFile_.empty())
        {
            file = currentFile_;
            value = host_.lookupKey(file, key);
        }
        if (!value)
            return fail(Diagnostic::Kind::MissingKey,
                        "no key '" + key + "' in " +
                            (argc == 2 ? file : "common strings" + (currentFile_.empty() ? "" : " or " + currentFile_)));
        if (depth + 1 >= kMaxDepth)
            return fail(Diagnostic::Kind::RecursionLimit, "nested expansion deeper than " + std::to_string(kMaxDepth));
        out.append(file.empty() ? expandArg(*value, depth + 1) : expandInFile(*value, file, depth + 1));
        return true;
    }
    case Builtin::Link: {
        if (!needArgs(1, 1))
            return true;
        const std::string key = arg(0);
        const auto value = host_.lookupLocalisation(key);
        if (!value)
            return fail(Diagnostic::Kind::MissingKey, "no localisation key '" + key + "'");
        out.append(expandValue(*value, name, depth));
        return true;
    }
    case Builtin::Value: {
        if (!needArgs(1, 1))
            return true;
        const std::string constant = arg(0);
        const auto value = host_.scriptConstant(constant);
        if (!value)
            return fail(Diagnostic::Kind::UnknownName, "no script constant '" + constant + "'");
        out.append(*value);
        return true;
    }
    case Builtin::Percent:
        if (needArgs(1, 1))
        {
            if (const auto value = number(call.raw))
            {
                const std::string digits = humanNumber(*value * 100);
                const std::string_view language = host_.language();
                if (language == "russian")
                    out.append(digits).append(" %");
                else if (language == "turkish")
                    out.append("%").append(digits);
                else
                    out.append(digits).append("%");
            }
        }
        return true;
    case Builtin::Number:
        if (needArgs(1, 1))
            if (const auto value = number(call.raw))
                out.append(humanNumber(*value));
        return true;
    case Builtin::Money:
        return !needArgs(1, 1) || script("FormatMoney", {text(arg(0))});
    case Builtin::Abs:
        if (needArgs(1, 1))
        {
            const std::string value = arg(0);
            out.append(std::string_view(value).substr(!value.empty() && value[0] == '-' ? 1 : 0));
        }
        return true;
    case Builtin::Days: {
        if (!needArgs(1, 1))
            return true;
        const auto value = number(call.raw);
        if (!value)
            return true;
        const auto n = static_cast<long long>(*value);
        std::array<std::string, 3> forms;
        constexpr std::array<std::string_view, 3> keys{"day2", "rdays2", "rdays"};
        for (size_t i = 0; i < keys.size(); ++i)
        {
            auto form = host_.lookupKey("", keys[i]);
            if (!form)
                return fail(Diagnostic::Kind::MissingKey, "$days needs common string '" + std::string(keys[i]) + "'");
            forms[i] = std::move(*form);
        }
        out.append(std::to_string(n)).append(" ").append(declensionForm(n, forms[0], forms[1], forms[2]));
        return true;
    }
    case Builtin::Decl: {
        if (!needArgs(3, 4))
            return true;
        const auto value = number(call.raw);
        if (!value)
            return true;
        const auto n = static_cast<long long>(*value);
        const std::string one = arg(1), few = arg(2), many = argc == 4 ? arg(3) : std::string();
        out.append(std::to_string(n)).append(" ").append(declensionForm(n, one, few, many));
        return true;
    }
    case Builtin::NationI:
        return !needArgs(1, 1) || script("GetNationNameByType", {text(arg(0))});
    case Builtin::GoodI:
        return !needArgs(1, 1) || script("DLG_GoodName", {text(arg(0))});
    case Builtin::ModifierPercent:
        return !needArgs(1, 1) ||
               script("DLG_HumanModifierValuePercent", {text(arg(0)), ref(ScriptArg::Kind::Context)});
    }
    return false;
}

std::string Expander::convert(const std::string_view file, const std::string_view key)
{
    const auto value = host_.lookupKey(file, key);
    if (!value)
    {
        report(Diagnostic::Kind::MissingKey, std::string(file) + ":" + std::string(key),
               "DLG_Convert: no key '" + std::string(key) + "' in " +
                   (file.empty() ? std::string("common strings") : std::string(file)));
        return std::string(key);
    }
    return expandInFile(*value, file, 0);
}

std::string Expander::convertOrEmpty(const std::string_view file, const std::string_view key)
{
    const auto value = host_.lookupKey(file, key);
    return value ? expandInFile(*value, file, 0) : std::string();
}

std::string Expander::stringFromKey(const std::string_view key, const std::span<const std::string> args)
{
    const auto value = host_.lookupLocalisation(key);
    if (!value)
    {
        report(Diagnostic::Kind::MissingKey, key, "StringFromKey: no key '" + std::string(key) + "'");
        return std::string(key);
    }
    std::string out;
    out.reserve(value->size() + 32);
    size_t used = 0;
    size_t pos = 0;
    while (pos < value->size())
    {
        const size_t open = value->find("@<", pos);
        const size_t close = open == std::string::npos ? std::string::npos : value->find('>', open + 2);
        if (close == std::string::npos)
        {
            out.append(*value, pos, std::string::npos);
            break;
        }
        out.append(*value, pos, open - pos);
        if (used < args.size())
            out.append(args[used]);
        else
        {
            report(Diagnostic::Kind::Placeholder, key,
                   "StringFromKey '" + std::string(key) + "': no argument for " + value->substr(open, close + 1 - open));
            out.append(*value, open, close + 1 - open);
        }
        ++used;
        pos = close + 1;
    }
    if (used < args.size())
        report(Diagnostic::Kind::Placeholder, key,
               "StringFromKey '" + std::string(key) + "': " + std::to_string(args.size() - used) + " unused argument(s)");
    return out;
}

// --- text files ---------------------------------------------------------------------------

size_t KeyHash::operator()(const std::string_view key) const noexcept
{
    uint64_t hash = 14695981039346656037ULL; // FNV-1a over ASCII-lowered bytes
    for (const char c : key)
    {
        hash ^= static_cast<unsigned char>(asciiLower(c));
        hash *= 1099511628211ULL;
    }
    return static_cast<size_t>(hash);
}

bool KeyEqual::operator()(const std::string_view a, const std::string_view b) const noexcept
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return asciiLower(x) == asciiLower(y); });
}

TextTable TextTable::parse(std::string content)
{
    TextTable table;
    table.content_ = std::make_unique<const std::string>(std::move(content));
    const std::string &all = *table.content_;
    std::string_view text = all;
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    const char *base = all.data();

    size_t lineAt = 0, line = 1; // incremental line counter for problem reports
    const auto lineOf = [&](const size_t pos) {
        const size_t absolute = static_cast<size_t>(text.data() - base) + pos;
        line += static_cast<size_t>(std::count(all.begin() + static_cast<std::ptrdiff_t>(lineAt),
                                               all.begin() + static_cast<std::ptrdiff_t>(absolute), '\n'));
        lineAt = absolute;
        return line;
    };

    size_t pos = 0;
    while (pos < text.size())
    {
        const size_t open = text.find('{', pos);
        std::string_view before = trim(text.substr(pos, open == std::string_view::npos ? std::string_view::npos : open - pos));
        if (open == std::string_view::npos)
        {
            if (!before.empty())
                table.problems_.push_back({lineOf(static_cast<size_t>(before.data() - text.data())),
                                           "text after the last value ignored"});
            break;
        }
        std::string_view key = before;
        if (const size_t newline = before.find_last_of('\n'); newline != std::string_view::npos)
        {
            table.problems_.push_back(
                {lineOf(static_cast<size_t>(before.data() - text.data())), "text between values ignored"});
            key = trim(before.substr(newline + 1));
        }
        const size_t close = text.find('}', open + 1);
        if (close == std::string_view::npos)
        {
            table.problems_.push_back({lineOf(open), "value without closing '}'"});
            break;
        }
        const std::string_view value = text.substr(open + 1, close - open - 1);
        if (value.find('{') != std::string_view::npos)
            table.problems_.push_back({lineOf(open), "'{' inside a value"});
        if (key.empty())
            table.problems_.push_back({lineOf(open), "value without key"});
        else if (table.index_.emplace(key, value).second)
            table.order_.push_back(key);
        else
            table.problems_.push_back({lineOf(open), "duplicate key '" + std::string(key) + "' (first kept)"});
        pos = close + 1;
    }
    return table;
}

std::optional<std::string_view> TextTable::find(const std::string_view key) const
{
    const auto it = index_.find(key);
    if (it == index_.end())
        return std::nullopt;
    return it->second;
}

std::string normaliseTextPath(const std::string_view path)
{
    std::string result(path);
    for (auto &c : result)
        c = c == '\\' ? '/' : asciiLower(c);
    return result;
}

TextCache::TextCache(Reader reader, const size_t byteBudget) : reader_(std::move(reader)), budget_(byteBudget)
{
}

const TextTable *TextCache::file(const std::string_view relativePath)
{
    std::string path = normaliseTextPath(relativePath);
    if (const auto it = index_.find(path); it != index_.end())
    {
        lru_.splice(lru_.begin(), lru_, it->second);
        return it->second->table ? &*it->second->table : nullptr;
    }
    Entry entry{path, std::nullopt};
    if (auto content = reader_(relativePath))
        entry.table = TextTable::parse(std::move(*content));
    constexpr size_t kEntryOverhead = 64;
    bytes_ += kEntryOverhead + (entry.table ? entry.table->bytes() : 0);
    lru_.push_front(std::move(entry));
    index_.emplace(std::move(path), lru_.begin());
    while (bytes_ > budget_ && lru_.size() > 1)
    {
        const Entry &last = lru_.back();
        bytes_ -= kEntryOverhead + (last.table ? last.table->bytes() : 0);
        index_.erase(last.path);
        lru_.pop_back();
    }
    return lru_.front().table ? &*lru_.front().table : nullptr;
}

void TextCache::clear()
{
    lru_.clear();
    index_.clear();
    bytes_ = 0;
}

} // namespace storm::dlg
