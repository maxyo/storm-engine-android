#include "storm/fx/parallax_exclusions.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace storm::fx
{
namespace
{
std::string Trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return std::string(s);
}

std::string WithoutComment(const std::string &line)
{
    return Trim(std::string_view(line).substr(0, line.find('#')));
}

bool IsName(const std::string &s)
{
    return !s.empty() && std::none_of(s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
}
} // namespace

std::string_view ParallaxExclusions::ShippedText()
{
    static constexpr std::string_view text =
#include "parallax_exclusions.inc"
        ;
    return text;
}

namespace
{
// `include <name>` / `exclude <name>` of a line without its comment; false when it is neither.
bool ParseLine(const std::string &content, std::string &name, bool &include)
{
    std::istringstream words(content);
    std::string verb, extra;
    words >> verb >> name;
    include = verb == "include";
    return !(words >> extra) && (verb == "exclude" || verb == "include") && !name.empty();
}
} // namespace

void ParallaxExclusions::LoadShipped(std::string_view text, std::vector<std::string> *problems)
{
    shipped_.clear();
    std::istringstream in{std::string(text)};
    std::string line;
    int number = 0;
    while (std::getline(in, line))
    {
        ++number;
        const std::string content = WithoutComment(line);
        if (content.empty())
            continue;
        std::string name;
        bool include = false;
        if (ParseLine(content, name, include))
            shipped_[name] = include;
        else if (problems)
            problems->push_back("shipped list line " + std::to_string(number) + ": '" + content +
                                "' is not `include <texture>` or `exclude <texture>`");
    }
}

bool ParallaxExclusions::LoadUser(const std::filesystem::path &file)
{
    user_.clear();
    user_file_ = file;
    std::error_code error;
    if (!std::filesystem::exists(file, error))
        return true;
    std::ifstream in(file);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::string name;
        bool include = false;
        if (ParseLine(WithoutComment(line), name, include))
            user_[name] = include;
    }
    return true;
}

ParallaxExclusions::Verdict ParallaxExclusions::Of(std::string_view normalized_name) const
{
    const std::string name(normalized_name);
    if (const auto it = user_.find(name); it != user_.end())
        return it->second ? Verdict::Included : Verdict::Excluded;
    if (const auto it = shipped_.find(name); it != shipped_.end())
        return it->second ? Verdict::Included : Verdict::Excluded;
    return Verdict::Default;
}

bool ParallaxExclusions::SetUser(std::string_view normalized_name, bool parallax)
{
    if (user_file_.empty() || !IsName(std::string(normalized_name)))
        return false;
    std::error_code error;
    std::filesystem::create_directories(user_file_.parent_path(), error);
    std::ofstream out(user_file_, std::ios::app);
    out << (parallax ? "include " : "exclude ") << normalized_name << '\n';
    if (!out.good())
        return false;
    user_[std::string(normalized_name)] = parallax;
    return true;
}

} // namespace storm::fx
