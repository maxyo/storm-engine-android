#include "natives_logic.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace storm::natives
{
namespace
{
uint32_t fnv1a(uint32_t h, std::string_view text)
{
    for (const unsigned char c : text)
    {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

constexpr uint32_t kFnvOffset = 2166136261u;

std::string_view skip(std::string_view value, int32_t offset)
{
    if (offset <= 0)
        return value;
    return value.substr(std::min<size_t>(static_cast<size_t>(offset), value.size()));
}
} // namespace

int32_t hash32(std::string_view text)
{
    return static_cast<int32_t>(fnv1a(kFnvOffset, text));
}

int32_t hashRandom(int32_t n, std::string_view global_seed, std::string_view call_site, std::string_view tag)
{
    if (n <= 0)
        return 0;
    uint32_t h = fnv1a(kFnvOffset, global_seed);
    h = fnv1a(h, "\x1f");
    if (tag.empty() || tag.front() != '&')
    {
        h = fnv1a(h, call_site);
        h = fnv1a(h, "\x1f");
    }
    h = fnv1a(h, tag);
    // final avalanche so that neighbouring sites do not give neighbouring numbers
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<int32_t>(h % (static_cast<uint32_t>(n) + 1u));
}

bool parseSortType(std::string_view name, SortType &type)
{
    if (name == "int")
        type = SortType::Int;
    else if (name == "float")
        type = SortType::Float;
    else if (name == "string")
        type = SortType::String;
    else if (name == "date")
        type = SortType::Date;
    else
        return false;
    return true;
}

SortKey makeSortKey(std::string_view value, int32_t offset, SortType type)
{
    SortKey key;
    const std::string text(skip(value, offset));
    switch (type)
    {
    case SortType::Int:
        key.number = static_cast<double>(std::strtoll(text.c_str(), nullptr, 10));
        break;
    case SortType::Float:
        key.number = std::strtod(text.c_str(), nullptr);
        break;
    case SortType::String:
        key.text = text;
        std::ranges::transform(key.text, key.text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        break;
    case SortType::Date: {
        // groups of digits; the year is the 4-digit group (first or third), the rest keep order
        std::vector<std::string_view> groups;
        size_t i = 0;
        while (i < text.size())
        {
            if (!std::isdigit(static_cast<unsigned char>(text[i])))
            {
                ++i;
                continue;
            }
            const size_t start = i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                ++i;
            groups.emplace_back(std::string_view(text).substr(start, i - start));
        }
        std::vector<int32_t> numbers;
        for (const auto g : groups)
            numbers.push_back(std::atoi(std::string(g).c_str()));
        if (numbers.size() >= 3 && groups[0].size() < 4 && groups[2].size() >= 4)
            std::swap(numbers[0], numbers[2]); // day.month.year -> year.month.day
        key.date = std::move(numbers);
        break;
    }
    }
    return key;
}

bool sortKeyLess(const SortKey &lhs, const SortKey &rhs, SortType type)
{
    switch (type)
    {
    case SortType::Int:
    case SortType::Float:
        return lhs.number < rhs.number;
    case SortType::String:
        return lhs.text < rhs.text;
    case SortType::Date:
        return lhs.date < rhs.date;
    }
    return false;
}

std::string formatScript(std::string_view fmt, const std::vector<FormatArg> &args, std::string &error)
{
    std::string out;
    size_t next = 0;
    size_t i = 0;
    while (i < fmt.size())
    {
        const char c = fmt[i];
        if (c != '%')
        {
            out += c;
            ++i;
            continue;
        }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%')
        {
            out += '%';
            i += 2;
            continue;
        }
        size_t j = i + 1;
        while (j < fmt.size() && std::string_view("-+ #0").find(fmt[j]) != std::string_view::npos)
            ++j;
        while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j])))
            ++j;
        if (j < fmt.size() && fmt[j] == '.')
        {
            ++j;
            while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j])))
                ++j;
        }
        const std::string_view spec = fmt.substr(i, j + 1 - i);
        if (j >= fmt.size() || std::string_view("diuxXocfFeEgGs").find(fmt[j]) == std::string_view::npos)
        {
            error = "unsupported conversion '" + std::string(fmt.substr(i, std::min(j + 1, fmt.size()) - i)) + "'";
            out.append(fmt.substr(i, std::min(j + 1, fmt.size()) - i));
            i = j + 1;
            continue;
        }
        if (next >= args.size())
        {
            error = "missing argument for '" + std::string(spec) + "'";
            out.append(spec);
            i = j + 1;
            continue;
        }
        const FormatArg &arg = args[next++];
        const char conv = fmt[j];
        std::string conversion(spec);
        char buffer[512];
        int written = 0;
        if (conv == 's')
        {
            std::string text;
            if (const auto *s = std::get_if<std::string>(&arg))
                text = *s;
            else if (const auto *v = std::get_if<int32_t>(&arg))
                text = std::to_string(*v);
            else
            {
                char f[64];
                std::snprintf(f, sizeof(f), "%g", static_cast<double>(std::get<float>(arg)));
                text = f;
            }
            written = std::snprintf(buffer, sizeof(buffer), conversion.c_str(), text.c_str());
        }
        else if (std::string_view("fFeEgG").find(conv) != std::string_view::npos)
        {
            double v = 0.0;
            if (const auto *f = std::get_if<float>(&arg))
                v = *f;
            else if (const auto *n = std::get_if<int32_t>(&arg))
                v = *n;
            else
                v = std::strtod(std::get<std::string>(arg).c_str(), nullptr);
            written = std::snprintf(buffer, sizeof(buffer), conversion.c_str(), v);
        }
        else
        {
            int32_t v = 0;
            if (const auto *n = std::get_if<int32_t>(&arg))
                v = *n;
            else if (const auto *f = std::get_if<float>(&arg))
                v = static_cast<int32_t>(*f);
            else
                v = static_cast<int32_t>(std::strtol(std::get<std::string>(arg).c_str(), nullptr, 10));
            if (conv == 'c')
                written = std::snprintf(buffer, sizeof(buffer), conversion.c_str(), static_cast<int>(v));
            else if (conv == 'd' || conv == 'i')
                written = std::snprintf(buffer, sizeof(buffer), conversion.c_str(), static_cast<int>(v));
            else
                written = std::snprintf(buffer, sizeof(buffer), conversion.c_str(), static_cast<unsigned>(v));
        }
        if (written > 0)
            out.append(buffer, std::min<size_t>(static_cast<size_t>(written), sizeof(buffer) - 1));
        i = j + 1;
    }
    return out;
}
} // namespace storm::natives
