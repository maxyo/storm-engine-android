#include "template.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace storm::ui
{

bool Truthy(const Json &value)
{
    switch (value.type())
    {
    case Json::value_t::null:
    case Json::value_t::discarded:
        return false;
    case Json::value_t::boolean:
        return value.get<bool>();
    case Json::value_t::string:
        return !value.get_ref<const std::string &>().empty();
    case Json::value_t::array:
    case Json::value_t::object:
        return !value.empty();
    default:
        return value.get<double>() != 0.0;
    }
}

static std::string NumberText(double number)
{
    if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < 1e15)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
        return buffer;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.4f", number);
    std::string text = buffer;
    while (!text.empty() && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

std::string ToText(const Json &value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_null() || value.is_discarded())
        return {};
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    if (value.is_number())
        return NumberText(value.get<double>());
    return value.dump();
}

std::string EscapeRml(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

namespace
{

double ToNumber(const Json &value)
{
    if (value.is_number())
        return value.get<double>();
    if (value.is_boolean())
        return value.get<bool>() ? 1.0 : 0.0;
    if (value.is_string())
        return std::strtod(value.get_ref<const std::string &>().c_str(), nullptr);
    return 0.0;
}

bool LooseEqual(const Json &a, const Json &b)
{
    if (a.is_number() && b.is_number())
        return a.get<double>() == b.get<double>();
    if (a.is_null() || b.is_null())
        return a.is_null() && b.is_null();
    if (a.is_string() != b.is_string() && (a.is_primitive() && b.is_primitive()))
        return ToText(a) == ToText(b);
    return a == b;
}

// Thousands separated by a thin no-break space, the way the design writes amounts.
std::string GroupDigits(const Json &value)
{
    std::string text = ToText(value);
    size_t start = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
    size_t end = text.find_first_not_of("0123456789", start);
    if (end == std::string::npos)
        end = text.size();
    if (end - start < 5) // 4-digit numbers stay solid, as in the mock-ups
        return text;
    std::string grouped = text.substr(0, start);
    for (size_t i = start; i < end; ++i)
    {
        if (i > start && (end - i) % 3 == 0)
            grouped += "\xE2\x80\xAF";
        grouped += text[i];
    }
    return grouped + text.substr(end);
}

class Parser
{
  public:
    Parser(std::string_view text, const Json &scope) : text_(text), scope_(scope)
    {
    }

    Json Run()
    {
        Json value = Ternary(true);
        Skip();
        if (pos_ != text_.size())
            Fail("unexpected text");
        return value;
    }

  private:
    [[noreturn]] void Fail(const char *what) const
    {
        throw std::runtime_error(std::string("template expression: ") + what + " in '" + std::string(text_) + "'");
    }

    void Skip()
    {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\t' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool Eat(std::string_view token)
    {
        Skip();
        if (text_.substr(pos_, token.size()) != token)
            return false;
        pos_ += token.size();
        return true;
    }

    // `live` is false in the branch not taken: it is parsed but has no effect.
    Json Ternary(bool live)
    {
        Json condition = Or(live);
        if (!Eat("?"))
            return condition;
        const bool pick = Truthy(condition);
        Json yes = Ternary(live && pick);
        if (!Eat(":"))
            Fail("expected ':'");
        Json no = Ternary(live && !pick);
        return pick ? yes : no;
    }

    Json Or(bool live)
    {
        Json left = And(live);
        while (true)
        {
            Skip();
            if (text_.substr(pos_, 2) != "||")
                return left;
            pos_ += 2;
            const bool have = Truthy(left);
            Json right = And(live && !have);
            if (!have)
                left = right;
        }
    }

    Json And(bool live)
    {
        Json left = Compare(live);
        while (true)
        {
            Skip();
            if (text_.substr(pos_, 2) != "&&")
                return left;
            pos_ += 2;
            const bool have = Truthy(left);
            Json right = Compare(live && have);
            if (have)
                left = right;
        }
    }

    Json Compare(bool live)
    {
        Json left = Sum(live);
        while (true)
        {
            if (Eat("=="))
                left = LooseEqual(left, Sum(live));
            else if (Eat("!="))
                left = !LooseEqual(left, Sum(live));
            else if (Eat("<="))
                left = ToNumber(left) <= ToNumber(Sum(live));
            else if (Eat(">="))
                left = ToNumber(left) >= ToNumber(Sum(live));
            else if (Eat("<"))
                left = ToNumber(left) < ToNumber(Sum(live));
            else if (Eat(">"))
                left = ToNumber(left) > ToNumber(Sum(live));
            else
                return left;
        }
    }

    Json Sum(bool live)
    {
        Json left = Product(live);
        while (true)
        {
            if (Eat("+"))
            {
                Json right = Product(live);
                if (left.is_string() || right.is_string())
                    left = ToText(left) + ToText(right);
                else
                    left = ToNumber(left) + ToNumber(right);
            }
            else if (Eat("-"))
                left = ToNumber(left) - ToNumber(Product(live));
            else
                return left;
        }
    }

    Json Product(bool live)
    {
        Json left = Unary(live);
        while (true)
        {
            if (Eat("*"))
                left = ToNumber(left) * ToNumber(Unary(live));
            else if (Eat("/"))
            {
                const double divisor = ToNumber(Unary(live));
                left = divisor == 0.0 ? 0.0 : ToNumber(left) / divisor;
            }
            else if (Eat("%"))
            {
                const double divisor = ToNumber(Unary(live));
                left = divisor == 0.0 ? 0.0 : std::fmod(ToNumber(left), divisor);
            }
            else
                return left;
        }
    }

    Json Unary(bool live)
    {
        Skip();
        if (pos_ < text_.size() && text_[pos_] == '!' && text_.substr(pos_, 2) != "!=")
        {
            ++pos_;
            return !Truthy(Unary(live));
        }
        if (Eat("-"))
            return -ToNumber(Unary(live));
        return Postfix(live);
    }

    Json Postfix(bool live)
    {
        Json value = Primary(live);
        while (true)
        {
            Skip();
            if (pos_ < text_.size() && text_[pos_] == '.')
            {
                ++pos_;
                const std::string key = Identifier();
                value = Member(value, key);
            }
            else if (pos_ < text_.size() && text_[pos_] == '[')
            {
                ++pos_;
                const Json index = Ternary(live);
                if (!Eat("]"))
                    Fail("expected ']'");
                if (index.is_number() && value.is_array())
                {
                    const auto at = static_cast<long long>(index.get<double>());
                    value = at >= 0 && static_cast<size_t>(at) < value.size() ? value[static_cast<size_t>(at)] : Json();
                }
                else
                    value = Member(value, ToText(index));
            }
            else
                return value;
        }
    }

    static Json Member(const Json &object, const std::string &key)
    {
        if (object.is_object())
        {
            const auto found = object.find(key);
            if (found != object.end())
                return *found;
        }
        return Json();
    }

    std::string Identifier()
    {
        Skip();
        const size_t start = pos_;
        while (pos_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_' || text_[pos_] == '$'))
            ++pos_;
        if (start == pos_)
            Fail("expected a name");
        return std::string(text_.substr(start, pos_ - start));
    }

    Json Primary(bool live)
    {
        Skip();
        if (pos_ >= text_.size())
            Fail("unexpected end");
        const char c = text_[pos_];
        if (c == '(')
        {
            ++pos_;
            Json value = Ternary(live);
            if (!Eat(")"))
                Fail("expected ')'");
            return value;
        }
        if (c == '\'' || c == '"')
        {
            const size_t end = text_.find(c, pos_ + 1);
            if (end == std::string_view::npos)
                Fail("unterminated string");
            Json value = std::string(text_.substr(pos_ + 1, end - pos_ - 1));
            pos_ = end + 1;
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && pos_ + 1 < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_ + 1]))))
        {
            const std::string rest(text_.substr(pos_));
            char *end = nullptr;
            const double number = std::strtod(rest.c_str(), &end);
            pos_ += static_cast<size_t>(end - rest.c_str());
            return number;
        }
        const std::string name = Identifier();
        if (name == "true")
            return true;
        if (name == "false")
            return false;
        if (name == "null")
            return Json();
        Skip();
        if (pos_ < text_.size() && text_[pos_] == '(')
        {
            ++pos_;
            std::vector<Json> arguments;
            if (!Eat(")"))
            {
                do
                    arguments.push_back(Ternary(live));
                while (Eat(","));
                if (!Eat(")"))
                    Fail("expected ')'");
            }
            return Call(name, arguments);
        }
        return Member(scope_, name);
    }

    Json Call(const std::string &name, const std::vector<Json> &a) const
    {
        const auto arg = [&a](size_t i) -> const Json & {
            static const Json none;
            return i < a.size() ? a[i] : none;
        };
        if (name == "or") // first non-null argument: or(size, 'md')
        {
            for (const Json &value : a)
                if (!value.is_null() && !(value.is_string() && value.get_ref<const std::string &>().empty()))
                    return value;
            return Json();
        }
        if (name == "len")
            return arg(0).is_string() ? arg(0).get_ref<const std::string &>().size() : (arg(0).is_structured() ? arg(0).size() : size_t(0));
        if (name == "num") // amount with grouped thousands
            return GroupDigits(arg(0));
        if (name == "str")
            return ToText(arg(0));
        if (name == "int")
            return std::floor(ToNumber(arg(0)));
        if (name == "round")
            return std::round(ToNumber(arg(0)));
        if (name == "min")
            return std::min(ToNumber(arg(0)), ToNumber(arg(1)));
        if (name == "max")
            return std::max(ToNumber(arg(0)), ToNumber(arg(1)));
        if (name == "clamp")
            return std::min(std::max(ToNumber(arg(0)), ToNumber(arg(1))), ToNumber(arg(2)));
        if (name == "abs")
            return std::fabs(ToNumber(arg(0)));
        if (name == "is_object")
            return arg(0).is_object();
        if (name == "is_array")
            return arg(0).is_array();
        if (name == "is_string")
            return arg(0).is_string();
        if (name == "has")
            return arg(0).is_object() && arg(0).contains(ToText(arg(1)));
        if (name == "in") // in(value, 'a', 'b', …)
        {
            for (size_t i = 1; i < a.size(); ++i)
                if (LooseEqual(a[0], a[i]))
                    return true;
            return false;
        }
        if (name == "starts")
            return ToText(arg(0)).rfind(ToText(arg(1)), 0) == 0;
        if (name == "contains")
            return ToText(arg(0)).find(ToText(arg(1))) != std::string::npos;
        if (name == "ends")
        {
            const std::string text = ToText(arg(0)), tail = ToText(arg(1));
            return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
        }
        if (name == "replace") // replace(text, from, to): every occurrence
        {
            std::string text = ToText(arg(0));
            const std::string from = ToText(arg(1)), to = ToText(arg(2));
            if (from.empty())
                return text;
            for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
                text.replace(at, from.size(), to);
            return text;
        }
        if (name == "split") // split('2/3', '/') → ['2', '3']
        {
            const std::string text = ToText(arg(0)), separator = ToText(arg(1));
            Json parts = Json::array();
            size_t at = 0;
            while (!separator.empty())
            {
                const size_t end = text.find(separator, at);
                if (end == std::string::npos)
                    break;
                parts.push_back(text.substr(at, end - at));
                at = end + separator.size();
            }
            parts.push_back(text.substr(at));
            return parts;
        }
        if (name == "range") // range(3) → [0, 1, 2]
        {
            Json list = Json::array();
            const int count = static_cast<int>(std::min(ToNumber(arg(0)), 1000.0));
            for (int i = 0; i < count; ++i)
                list.push_back(i);
            return list;
        }
        if (name == "digits") // number written with spaces or thin spaces → number
        {
            std::string kept;
            for (const char c : ToText(arg(0)))
                if ((c >= '0' && c <= '9') || c == '-' || c == '.')
                    kept += c;
                else if (c == ',')
                    kept += '.';
            return kept.empty() ? Json() : Json(std::strtod(kept.c_str(), nullptr));
        }
        if (name == "json") // for passing a structure on to a nested component attribute
            return arg(0).dump();
        if (name == "image_kind") // image_kind(image, icon): the lead of a row (one row-image API)
        {
            // image = {kind: icon|token|item|portrait|emblem, icon, enamel, texture, cell}; a bare
            // `icon` is the shorthand for {kind: 'icon'}; nothing at all = no lead ('').
            const Json &image = arg(0);
            if (image.is_object())
            {
                const std::string kind = image.contains("kind") ? ToText(image["kind"]) : std::string();
                if (!kind.empty())
                    return kind;
                if (!ToText(image.value("texture", Json())).empty())
                    return "item";
                if (!ToText(image.value("enamel", Json())).empty())
                    return "token";
                if (!ToText(image.value("icon", Json())).empty())
                    return "icon";
            }
            return ToText(arg(1)).empty() ? "" : "icon";
        }
        if (name == "pick") // pick(key, 'a', x, 'b', y, fallback)
        {
            size_t i = 1;
            for (; i + 1 < a.size(); i += 2)
                if (LooseEqual(a[0], a[i]))
                    return a[i + 1];
            return i < a.size() ? a[i] : Json();
        }
        Fail(("unknown function " + name).c_str());
    }

    std::string_view text_;
    const Json &scope_;
    size_t pos_ = 0;
};

struct Tag
{
    size_t begin = std::string_view::npos; // position of '<'
    size_t end = 0;                        // position after '>'
    bool closing = false;
    bool self_closing = false;
    std::string_view name;
    std::string_view attributes;
};

// Finds the next <t-…> or </t-…> tag at or after `from`.
Tag NextControlTag(std::string_view source, size_t from)
{
    Tag tag;
    size_t at = from;
    while ((at = source.find('<', at)) != std::string_view::npos)
    {
        size_t name_at = at + 1;
        const bool closing = name_at < source.size() && source[name_at] == '/';
        if (closing)
            ++name_at;
        if (source.substr(name_at, 2) != "t-")
        {
            ++at;
            continue;
        }
        // Attribute values are quoted and may hold '>', so honour the quotes.
        size_t close = name_at;
        char quote = 0;
        for (; close < source.size(); ++close)
        {
            const char c = source[close];
            if (quote)
            {
                if (c == quote)
                    quote = 0;
            }
            else if (c == '"' || c == '\'')
                quote = c;
            else if (c == '>')
                break;
        }
        if (close >= source.size())
            throw std::runtime_error("template: unterminated control tag");
        size_t name_end = name_at;
        while (name_end < close && (std::isalnum(static_cast<unsigned char>(source[name_end])) || source[name_end] == '-'))
            ++name_end;
        tag.begin = at;
        tag.end = close + 1;
        tag.closing = closing;
        tag.self_closing = close > name_end && source[close - 1] == '/';
        tag.name = source.substr(name_at, name_end - name_at);
        tag.attributes = source.substr(name_end, close - name_end - (tag.self_closing ? 1 : 0));
        return tag;
    }
    return tag;
}

std::string Attribute(std::string_view attributes, std::string_view name)
{
    size_t at = 0;
    while ((at = attributes.find(name, at)) != std::string_view::npos)
    {
        const size_t after = at + name.size();
        if ((at == 0 || attributes[at - 1] == ' ' || attributes[at - 1] == '\n' || attributes[at - 1] == '\t') && after < attributes.size() &&
            attributes[after] == '=')
        {
            const char quote = after + 1 < attributes.size() ? attributes[after + 1] : 0;
            if (quote != '"' && quote != '\'')
                break;
            const size_t end = attributes.find(quote, after + 2);
            if (end == std::string_view::npos)
                break;
            return std::string(attributes.substr(after + 2, end - after - 2));
        }
        at = after;
    }
    throw std::runtime_error("template: control tag lacks attribute " + std::string(name));
}

// Finds the tag closing the block opened just before `from`, skipping nested blocks.
// Reports a top-level <t-else/> of a t-if through `else_tag`.
Tag MatchingClose(std::string_view source, size_t from, std::string_view name, Tag *else_tag)
{
    int depth = 0;
    size_t at = from;
    while (true)
    {
        Tag tag = NextControlTag(source, at);
        if (tag.begin == std::string_view::npos)
            throw std::runtime_error("template: unclosed <" + std::string(name) + ">");
        at = tag.end;
        if (tag.name == "t-else")
        {
            if (depth == 0 && else_tag && else_tag->begin == std::string_view::npos)
                *else_tag = tag;
            continue;
        }
        if (tag.closing)
        {
            if (depth == 0)
            {
                if (tag.name != name)
                    throw std::runtime_error("template: mismatched </" + std::string(tag.name) + ">");
                return tag;
            }
            --depth;
        }
        else if (!tag.self_closing)
            ++depth;
    }
}

void RenderText(std::string_view source, const Json &scope, std::string &out)
{
    size_t at = 0;
    while (true)
    {
        const size_t open = source.find("{{", at);
        if (open == std::string_view::npos)
        {
            out += source.substr(at);
            return;
        }
        out += source.substr(at, open - at);
        const bool raw = source.substr(open, 3) == "{{{";
        const std::string_view closer = raw ? "}}}" : "}}";
        const size_t start = open + closer.size();
        const size_t close = source.find(closer, start);
        if (close == std::string_view::npos)
            throw std::runtime_error("template: unterminated {{");
        const std::string text = ToText(Parser(source.substr(start, close - start), scope).Run());
        out += raw ? text : EscapeRml(text);
        at = close + closer.size();
    }
}

void RenderBlock(std::string_view source, const Json &scope, std::string &out)
{
    size_t at = 0;
    while (true)
    {
        const Tag tag = NextControlTag(source, at);
        if (tag.begin == std::string_view::npos)
        {
            RenderText(source.substr(at), scope, out);
            return;
        }
        RenderText(source.substr(at, tag.begin - at), scope, out);
        if (tag.closing || tag.name == "t-else")
            throw std::runtime_error("template: stray <" + std::string(tag.name) + ">");
        if (tag.name == "t-if")
        {
            Tag else_tag;
            const Tag close = MatchingClose(source, tag.end, "t-if", &else_tag);
            const bool pick = Truthy(Parser(Attribute(tag.attributes, "test"), scope).Run());
            const bool has_else = else_tag.begin != std::string_view::npos;
            if (pick)
                RenderBlock(source.substr(tag.end, (has_else ? else_tag.begin : close.begin) - tag.end), scope, out);
            else if (has_else)
                RenderBlock(source.substr(else_tag.end, close.begin - else_tag.end), scope, out);
            at = close.end;
        }
        else if (tag.name == "t-for")
        {
            const Tag close = MatchingClose(source, tag.end, "t-for", nullptr);
            const Json list = Parser(Attribute(tag.attributes, "each"), scope).Run();
            const std::string as = Attribute(tag.attributes, "as");
            const std::string_view body = source.substr(tag.end, close.begin - tag.end);
            if (list.is_array())
            {
                Json inner = scope;
                size_t index = 0;
                for (const Json &item : list)
                {
                    inner[as] = item;
                    inner[as + "_i"] = index;
                    inner[as + "_last"] = index + 1 == list.size();
                    RenderBlock(body, inner, out);
                    ++index;
                }
            }
            at = close.end;
        }
        else
            throw std::runtime_error("template: unknown <" + std::string(tag.name) + ">");
    }
}

} // namespace

Json EvaluateExpression(std::string_view expression, const Json &scope)
{
    return Parser(expression, scope).Run();
}

std::string RenderTemplate(std::string_view source, const Json &scope)
{
    std::string out;
    out.reserve(source.size() * 2);
    RenderBlock(source, scope, out);
    return out;
}

} // namespace storm::ui
