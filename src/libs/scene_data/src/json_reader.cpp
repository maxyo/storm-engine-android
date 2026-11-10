#include "json_reader.hpp"

#include <algorithm>
#include <cctype>

namespace storm::scene_data
{

std::string TexturePath(std::string_view reference)
{
    std::string out;
    for (char c : reference)
    {
        if (c == '/')
            c = '\\';
        if (c == '\\' && !out.empty() && out.back() == '\\')
            continue;
        out.push_back(c);
    }
    std::string lower = out;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    constexpr std::string_view prefix = "resource\\textures\\";
    if (lower.rfind(prefix, 0) == 0)
        out.erase(0, prefix.size());
    return out;
}

namespace detail
{

bool Reader::Parse(std::string_view text, Json &out)
{
    out = Json::parse(text.begin(), text.end(), nullptr, false);
    if (out.is_discarded())
    {
        Error("$", "malformed JSON");
        return false;
    }
    return true;
}

void Reader::Object(const Json &object, const std::string &path, const Field &field)
{
    if (!object.is_object())
    {
        Error(path, "expected an object");
        return;
    }
    for (const auto &[key, value] : object.items())
    {
        const std::string child = path + "." + key;
        if (!field(key, value, child))
            report_.unknown_keys.push_back(child);
    }
}

bool Reader::Float(const Json &value, const std::string &path, float &out)
{
    if (!value.is_number())
    {
        Error(path, "expected a number");
        return false;
    }
    out = value.get<float>();
    return true;
}

bool Reader::Int(const Json &value, const std::string &path, int &out)
{
    if (!value.is_number_integer())
    {
        Error(path, "expected an integer");
        return false;
    }
    out = value.get<int>();
    return true;
}

bool Reader::Bool(const Json &value, const std::string &path, bool &out)
{
    if (value.is_boolean())
        out = value.get<bool>();
    else if (value.is_number_integer() && (value.get<int64_t>() == 0 || value.get<int64_t>() == 1))
        out = value.get<int64_t>() == 1;
    else
    {
        Error(path, "expected a boolean or 0/1");
        return false;
    }
    return true;
}

bool Reader::String(const Json &value, const std::string &path, std::string &out)
{
    if (!value.is_string())
    {
        Error(path, "expected a string");
        return false;
    }
    out = value.get<std::string>();
    return true;
}

bool Reader::Color(const Json &value, const std::string &path, Rgb &out)
{
    if (!value.is_array() || value.size() != 3 || !std::ranges::all_of(value, [](const Json &v) { return v.is_number(); }))
    {
        Error(path, "expected an array of 3 numbers");
        return false;
    }
    for (size_t i = 0; i < 3; ++i)
        out[i] = value[i].get<float>();
    return true;
}

bool Reader::StringList(const Json &value, const std::string &path, std::vector<std::string> &out)
{
    if (!value.is_array() || !std::ranges::all_of(value, [](const Json &v) { return v.is_string(); }))
    {
        Error(path, "expected an array of strings");
        return false;
    }
    out.clear();
    for (const Json &v : value)
        out.push_back(v.get<std::string>());
    return true;
}

void Reader::Error(const std::string &path, const std::string &what) { report_.errors.push_back(path + ": " + what); }

} // namespace detail
} // namespace storm::scene_data
