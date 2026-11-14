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

void ParallaxExclusions::LoadShipped(std::string_view text, std::vector<std::string> *problems)
{
    shipped_.clear();
    std::istringstream in{std::string(text)};
    std::string line;
    int number = 0;
    while (std::getline(in, line))
    {
        ++number;
        const std::string name = WithoutComment(line);
        if (name.empty())
            continue;
        if (IsName(name))
            shipped_[name] = false;
        else if (problems)
            problems->push_back("shipped list line " + std::to_string(number) + ": '" + name + "' is not a texture name");
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
        const std::string content = WithoutComment(line);
        std::istringstream words(content);
        std::string verb, name, extra;
        words >> verb >> name;
        if (!(words >> extra) && (verb == "exclude" || verb == "include") && !name.empty())
            user_[name] = verb == "include";
    }
    return true;
}

ParallaxExclusions::Verdict ParallaxExclusions::Of(std::string_view normalized_name) const
{
    const std::string name(normalized_name);
    if (const auto it = user_.find(name); it != user_.end())
        return it->second ? Verdict::Included : Verdict::Excluded;
    return shipped_.count(name) ? Verdict::Excluded : Verdict::Default;
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
